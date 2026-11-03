// My own VPN provider bridge. I write this as real extension code so my proxy
// transport is driven through the same gated chrome.* surface a third-party VPN
// would use. This is not Proton VPN and I do not claim it is.
//
// I keep this to the JavaScript my own engine actually implements: no classes,
// no modules, no promises. Callbacks only.

var DEFAULTS = {
  enabled: false,
  scheme: 'http',
  host: '',
  port: 0,
  failClosed: true,
  bypass: ['localhost', '127.0.0.1', '<local>']
};

var state = DEFAULTS;

function describe() {
  if (!state.enabled) return 'VPN off';
  if (!state.host || !state.port) return 'VPN not configured';
  return 'VPN ' + state.scheme + '://' + state.host + ':' + state.port;
}

function publishBadge() {
  console.log('My VPN state: ' + describe());
  chrome.action.setBadgeText({ text: state.enabled ? 'ON' : '' });
  chrome.action.setTitle({ title: describe() });
}

function applyRouting() {
  if (!state.enabled) {
    chrome.proxy.settings.clear({});
    // I put WebRTC back to the platform default when I am not tunnelling.
    chrome.privacy.network.webRTCIPHandlingPolicy.set({ value: 'default' });
    publishBadge();
    return;
  }
  if (!state.host || !state.port) {
    // I refuse to claim a VPN is on when I have nowhere to send traffic.
    state.enabled = false;
    chrome.storage.local.set({ enabled: false });
    chrome.proxy.settings.clear({});
    publishBadge();
    return;
  }
  chrome.proxy.settings.set({
    value: {
      mode: 'fixed_servers',
      singleProxy: { scheme: state.scheme, host: state.host, port: state.port },
      bypassList: state.bypass,
      failClosed: state.failClosed
    }
  });
  // I only allow proxied UDP so a WebRTC candidate cannot bypass my tunnel.
  // My engine has no WebRTC stack yet, so this records policy rather than
  // claiming a leak test passed.
  chrome.privacy.network.webRTCIPHandlingPolicy.set({ value: 'disable_non_proxied_udp' });
  publishBadge();
}

function loadThenApply() {
  chrome.storage.local.get(DEFAULTS, function (stored) {
    state = stored;
    if (!state.bypass || !state.bypass.length) state.bypass = DEFAULTS.bypass;
    applyRouting();
  });
}

// I answer only proxy challenges. My broker never gives me website credentials.
chrome.webRequest.onAuthRequired.addListener(function (details, callback) {
  if (!details || !details.isProxy) {
    if (callback) callback({});
    return;
  }
  chrome.storage.local.get({ username: '', password: '' }, function (creds) {
    if (!creds.username) {
      if (callback) callback({});
      return;
    }
    if (callback) {
      callback({ authCredentials: { username: creds.username, password: creds.password } });
    }
  });
}, { urls: ['<all_urls>'] }, ['asyncBlocking']);

chrome.proxy.onProxyError.addListener(function (error) {
  console.error('My VPN proxy reported an error: ' + (error && error.error ? error.error : 'unknown'));
  if (state.failClosed) {
    // I keep the proxy installed so my broker keeps refusing rather than
    // silently falling back to a direct connection.
    console.warn('I am holding fail-closed instead of connecting directly.');
  }
});

chrome.alarms.create('psychovault-vpn-keepalive', { periodInMinutes: 1 });

chrome.alarms.onAlarm.addListener(function (alarm) {
  if (alarm.name !== 'psychovault-vpn-keepalive') return;
  // I re-assert my routing so another extension cannot quietly take over.
  chrome.proxy.settings.get({}, function (current) {
    var value = current && current.value ? current.value : {};
    if (state.enabled && value.mode !== 'fixed_servers') applyRouting();
  });
});

chrome.runtime.onMessage.addListener(function (message) {
  if (!message || message.target !== 'psychovault-vpn') return;
  if (message.command === 'status') {
    chrome.runtime.sendMessage({ target: 'psychovault-vpn-ui', state: state, summary: describe() });
    return;
  }
  if (message.command === 'configure') {
    state.scheme = message.scheme === 'https' || message.scheme === 'socks5' ? message.scheme : 'http';
    state.host = message.host || '';
    state.port = message.port || 0;
    state.enabled = !!message.enabled;
    chrome.storage.local.set({
      scheme: state.scheme, host: state.host, port: state.port, enabled: state.enabled
    });
    applyRouting();
    return;
  }
  if (message.command === 'disconnect') {
    state.enabled = false;
    chrome.storage.local.set({ enabled: false });
    applyRouting();
  }
});

loadThenApply();
console.log('My VPN bridge started. ' + describe());
