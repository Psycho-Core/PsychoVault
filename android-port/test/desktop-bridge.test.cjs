const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {create} = require('../ui/desktop-bridge');
test('adapter exposes the same browser method names as the desktop preload',()=>{
  const source=fs.readFileSync(path.join(__dirname,'../../app/preload.js'),'utf8');
  const expected={};let group=null;
  for(const line of source.split('\n')) {
    const opening=line.match(/^  (\w+): \{$/); if(opening){group=opening[1];expected[group]=[];continue;}
    if(/^  },?$/.test(line)){group=null;continue;}
    if(group){const m=line.match(/^    (\w+): /);if(m)expected[group].push(m[1]);}
  }
  const api=create({invoke(){},subscribe(){}});
  for(const [key,methods] of Object.entries(expected)) assert.deepEqual(Object.keys(api[key]).sort(),methods.sort(),key);
  assert.equal(typeof api.openExternal,'function'); assert.equal(typeof api.on,'function');
});
test('commands forward explicit desktop channels and expected arguments',async()=>{
  const calls=[]; const api=create({invoke:(name,args)=>{calls.push([name,args]);return 'ok';},subscribe(){}});
  assert.equal(await api.nav.go('https://example.com'),'ok');await api.ws.rename(2,'Work');await api.rail.menu(1);
  assert.deepEqual(calls,[['nav:go',['https://example.com']],['ws:rename',[{i:2,name:'Work'}]],['gx:services',[{i:1,action:'menu'}]]]);
});
test('rejects unrecognised events and propagates native failures',async()=>{
  const api=create({invoke(){throw Error('not implemented');},subscribe:(event,cb)=>({event,cb})});
  assert.throws(()=>api.on('arbitrary',()=>{}),/Unsupported browser event/);
  assert.equal(api.on('state',()=>{}).event,'state');
  await assert.rejects(api.extensions.list(),/not implemented/);assert.throws(()=>create({}),/native Chromium transport/);
});
