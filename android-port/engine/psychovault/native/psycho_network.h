#ifndef PSYCHOVAULT_NATIVE_PSYCHO_NETWORK_H_
#define PSYCHOVAULT_NATIVE_PSYCHO_NETWORK_H_

#include <memory>
#include <string>
#include <vector>

#include "third_party/blink/public/common/loader/url_loader_throttle.h"

class Profile;
namespace content { class WebContents; }

namespace psychovault {
// Called on Chromium's UI thread. The returned throttle owns an immutable policy
// snapshot, so changing a setting cannot race a request on another sequence.
std::unique_ptr<blink::URLLoaderThrottle> CreateNetworkThrottle(
    Profile* profile, content::WebContents* contents);
void UpdateNetworkPolicy(Profile* profile, const std::string& json);
void ForgetNetworkProfile(Profile* profile);
}  // namespace psychovault
#endif
