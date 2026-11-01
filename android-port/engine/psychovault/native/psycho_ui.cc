#include "psychovault/native/psycho_ui.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/android/apk_assets.h"
#include "base/files/memory_mapped_file.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/common/url_constants.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/webui_config_map.h"
#include "third_party/skia/include/core/SkColor.h"
#include "url/gurl.h"

namespace psychovault {
namespace {
constexpr char kChromeHost[] = "psychovault-chrome";
constexpr char kPageHost[] = "psychovault-pages";

// Explicit allowlist: no path traversal, directory listing, recovery handlers
// or arbitrary APK assets are exposed. Settings is a page-origin document only.
const std::map<std::string, std::string>& Assets() {
  static const auto* assets = new std::map<std::string, std::string>{
      {"index.html", "text/html"}, {"style.css", "text/css"},
      {"mobile.css", "text/css"}, {"renderer.js", "text/javascript"},
      {"sidebar.js", "text/javascript"}, {"mobile-shell.js", "text/javascript"},
      {"desktop-bridge.js", "text/javascript"}, {"internal-page-bridge.js", "text/javascript"},
      {"navigation-resolver.js", "text/javascript"}, {"native-transport.js", "text/javascript"},
      {"host-layout.js", "text/javascript"}, {"chrome-bootstrap.js", "text/javascript"},
      {"library.js", "text/javascript"}, {"library-transport.js", "text/javascript"},
      {"indexeddb-storage.js", "text/javascript"}, {"newtab.html", "text/html"},
      {"newtab.js", "text/javascript"}, {"page-bootstrap.js", "text/javascript"},
      {"settings.html", "text/html"}, {"settings.js", "text/javascript"},
      {"icon.png", "image/png"}};
  return *assets;
}

class PsychoAssetSource : public content::URLDataSource {
 public:
  explicit PsychoAssetSource(std::string host) : host_(std::move(host)) {}
  std::string GetSource() override { return host_; }
  std::string GetMimeType(const GURL& url) override {
    auto it = Assets().find(url.path_piece().substr(1));
    return it == Assets().end() ? "text/plain" : it->second;
  }
  bool ShouldServeMimeTypeAsContentTypeHeader() override { return true; }
  std::string GetContentSecurityPolicy(network::mojom::CSPDirectiveName name) override {
    using Directive = network::mojom::CSPDirectiveName;
    if (name == Directive::ScriptSrc) return "script-src 'self';";
    if (name == Directive::StyleSrc) return "style-src 'self' 'unsafe-inline';";
    if (name == Directive::ImgSrc) return "img-src 'self' data:;";
    if (name == Directive::ConnectSrc) return "connect-src 'none';";
    return content::URLDataSource::GetContentSecurityPolicy(name);
  }
  void StartDataRequest(const GURL& url, const content::WebContents::Getter&,
                        GotDataCallback callback) override {
    const std::string path(url.path_piece().substr(1));
    auto it = Assets().find(path);
    const bool chrome = host_ == kChromeHost;
    const bool allowed = it != Assets().end() &&
        (chrome ? (path != "newtab.html" && path != "newtab.js" && path != "page-bootstrap.js" &&
                   path != "settings.html" && path != "settings.js")
                : (path == "newtab.html" || path == "newtab.js" || path == "page-bootstrap.js" ||
                  path == "settings.html" || path == "settings.js" ||
                  path == "navigation-resolver.js" || path == "native-transport.js" ||
                  path == "internal-page-bridge.js" || path == "icon.png"));
    std::string bytes;
    if (allowed) {
      base::MemoryMappedFile::Region region;
      int fd = base::android::OpenApkAsset("assets/psychovault/ui/" + path, &region);
      base::MemoryMappedFile file;
      if (fd >= 0 && file.Initialize(fd, region) && file.length() <= 4 * 1024 * 1024) {
        bytes.assign(reinterpret_cast<const char*>(file.data()), file.length());
      }
    }
    if (bytes.empty()) bytes = "<!doctype html><title>Unavailable</title><p>This Android UI asset is unavailable.</p>";
    std::move(callback).Run(base::RefCountedString::TakeString(&bytes));
  }
 private:
  std::string host_;
};
}  // namespace

PsychoUI::PsychoUI(content::WebUI* web_ui) : WebUIController(web_ui) {
  auto* profile = Profile::FromWebUI(web_ui);
  content::URLDataSource::Add(profile,
      std::make_unique<PsychoAssetSource>(std::string(web_ui->GetWebContents()->GetVisibleURL().host())));
}
PsychoUI::~PsychoUI() = default;
void PsychoUI::WebUIRenderFrameCreated(content::RenderFrameHost* frame) {
  if (frame->GetRenderWidgetHost()->GetView()) {
    frame->GetRenderWidgetHost()->GetView()->SetBackgroundColor(SK_ColorTRANSPARENT);
  }
}
PsychoUI::DisplayDisposition PsychoUI::GetDisplayDisposition() const {
  return web_ui()->GetWebContents()->GetVisibleURL().host() == kChromeHost
      ? DisplayDisposition::kUIElement : DisplayDisposition::kRegularPage;
}
PsychoUIConfig::PsychoUIConfig(std::string_view host)
    : DefaultWebUIConfig(content::kChromeUIScheme, host) {}
void RegisterPsychoWebUIConfigs() {
  auto& map = content::WebUIConfigMap::GetInstance();
  map.AddWebUIConfig(std::make_unique<PsychoUIConfig>(kChromeHost));
  map.AddWebUIConfig(std::make_unique<PsychoUIConfig>(kPageHost));
}
}  // namespace psychovault
