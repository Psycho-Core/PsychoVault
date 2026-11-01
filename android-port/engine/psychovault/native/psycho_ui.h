#ifndef PSYCHOVAULT_NATIVE_PSYCHO_UI_H_
#define PSYCHOVAULT_NATIVE_PSYCHO_UI_H_
#include <string_view>
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
namespace psychovault {
// Assets only. Commands use an origin-matched Java bridge, never a handler in website tabs.
class PsychoUI : public content::WebUIController {
 public:
  explicit PsychoUI(content::WebUI* web_ui);
  ~PsychoUI() override;
  void WebUIRenderFrameCreated(content::RenderFrameHost* frame) override;
  DisplayDisposition GetDisplayDisposition() const override;
};
class PsychoUIConfig : public content::DefaultWebUIConfig<PsychoUI> {
 public:
  explicit PsychoUIConfig(std::string_view host);
};
void RegisterPsychoWebUIConfigs();
}  // namespace psychovault
#endif
