const {test} = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');
const {create:chromeBridge}=require('../ui/desktop-bridge');
const {create:pageBridge}=require('../ui/internal-page-bridge');
function desktop(relative) {
  let api; const calls=[];
  const electron={contextBridge:{exposeInMainWorld:(name,value)=>{assert.equal(name,'pv');api=value;}},ipcRenderer:{invoke:(...args)=>{calls.push(args);return Promise.resolve();},on(){}}};
  vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../../app',relative),'utf8'),{require:name=>{assert.equal(name,'electron');return electron;},window:{location:{href:'file:///app/ui/settings.html'}}});
  return {api,calls};
}
for (const [name,file,factory] of [['chrome','preload.js',chromeBridge],['internal page','page-preload.js',pageBridge]]) {
  test(name+' adapter covers desktop method names and forwards the same channels and arguments',async()=>{
    const original=desktop(file), calls=[];const adapter=factory({invoke:(channel,args)=>{calls.push([channel,...args]);return Promise.resolve();},subscribe(){}});
    const groups=Object.keys(original.api).filter(k=>typeof original.api[k]==='object');
    assert.deepEqual(Object.keys(adapter).sort(),Object.keys(original.api).sort());
    for (const group of groups) {
      assert.deepEqual(Object.keys(adapter[group]).sort(),Object.keys(original.api[group]).sort(),group);
      for(const method of Object.keys(original.api[group])) {
        original.calls.length=0;calls.length=0;
        await original.api[group][method]('first','second');await adapter[group][method]('first','second');
        assert.deepEqual(JSON.parse(JSON.stringify(calls)),JSON.parse(JSON.stringify(original.calls)),group+'.'+method);
      }
    }
    original.calls.length=0;calls.length=0;await original.api.openExternal('https://example.com');await adapter.openExternal('https://example.com');assert.deepEqual(calls,original.calls);
  });
}
test('internal page event access is narrower than chrome event access',()=>{
  const events=[];const api=pageBridge({invoke(){},subscribe:name=>events.push(name)});api.on('spell',()=>{});api.on('extensions',()=>{});assert.deepEqual(events,['spell','extensions']);assert.throws(()=>api.on('state',()=>{}),/Unsupported/);
});
