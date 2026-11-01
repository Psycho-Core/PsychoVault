const {test}=require('node:test');
const assert=require('node:assert/strict');
const {inventory}=require('../scripts/audit-desktop-settings.cjs');
test('every desktop setting is accounted for without mistaking UI adapters for implementations',()=>{
  assert.equal(inventory.length,59);
  assert.equal(new Set(inventory.map(x=>x.key)).size,inventory.length);
  assert.equal(inventory.filter(x=>x.status==='desktop-internal-migration-marker').length,1);
  assert.equal(inventory.filter(x=>x.status==='android-source-implemented-not-device-verified').length,11);
  assert.equal(inventory.filter(x=>x.status==='android-extension-manager-source-not-device-verified').length,1);
  assert.equal(inventory.filter(x=>x.status==='psychovault-android-host-not-implemented').length,46);
  assert.ok(inventory.some(x=>x.key==='extensions'&&x.status==='android-extension-manager-source-not-device-verified'));
});
