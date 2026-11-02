'use strict';
// PsychoVault's own extension-engine core. MIT licensed. This is a policy and
// dispatch layer, not a renderer sandbox and not a complete browser.

module.exports = {
  ...require('./manifest'),
  ...require('./permissions'),
  ...require('./dispatcher')
};
