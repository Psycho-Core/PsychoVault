const {test} = require('node:test');
const assert = require('node:assert/strict');
const {create} = require('../ui/sidebar');
function harness() {
  const timers=new Map();let next=0;const changes=[];
  const element=()=>({hidden:false,attributes:{},listeners:new Map(),children:new Set(),setAttribute(k,v){this.attributes[k]=v;},addEventListener(k,v){this.listeners.set(k,v);},removeEventListener(k,v){if(this.listeners.get(k)===v)this.listeners.delete(k);},contains(v){return this.children.has(v);},focus(){this.focused=true;},emit(k,event={}){this.listeners.get(k)?.(event);}});
  const rail=element(),toggle=element(),document=element();
  const controller=create({rail,toggle,document,setTimer:(fn,ms)=>{const id=++next;timers.set(id,{fn,ms});return id;},clearTimer:id=>timers.delete(id),onVisibility:shown=>changes.push(shown)});
  return {rail,toggle,document,timers,controller,changes};
}
test('sidebar starts hidden, opens by toggle and auto-hides after five seconds',()=>{
  const h=harness();assert.equal(h.rail.hidden,true);assert.equal(h.toggle.attributes['aria-expanded'],'false');h.toggle.emit('click');assert.equal(h.rail.hidden,false);assert.equal(h.toggle.attributes['aria-expanded'],'true');
  assert.equal(h.timers.size,1);assert.equal([...h.timers.values()][0].ms,5000);[...h.timers.values()][0].fn();assert.equal(h.rail.hidden,true);assert.equal(h.timers.size,0);
});
test('interacting with the rail resets the timer and choosing a button closes it',()=>{
  const h=harness();h.controller.show();const old=[...h.timers.keys()][0];h.rail.emit('pointerdown');assert.equal(h.timers.has(old),false);assert.equal(h.timers.size,1);
  h.rail.emit('click',{target:{closest:()=>({})}});assert.equal(h.rail.hidden,true);assert.equal(h.timers.size,0);
});
test('page touch, outside touch, escape and toggle close the rail',()=>{
  const h=harness();h.controller.show();h.controller.pageTouched();assert.equal(h.rail.hidden,true);
  h.controller.show();h.document.emit('pointerdown',{target:{}});assert.equal(h.rail.hidden,true);
  h.controller.show();h.document.emit('keydown',{key:'Escape'});assert.equal(h.rail.hidden,true);assert.equal(h.toggle.focused,true);
  h.controller.show();h.toggle.emit('click');assert.equal(h.rail.hidden,true);
});
test('inside touch does not close the rail and dispose removes listeners and timers',()=>{
  const h=harness(), child={};h.rail.children.add(child);h.controller.show();h.document.emit('pointerdown',{target:child});assert.equal(h.rail.hidden,false);h.controller.dispose();assert.equal(h.timers.size,0);assert.equal(h.document.listeners.size,0);assert.equal(h.rail.listeners.size,0);assert.equal(h.toggle.listeners.size,0);h.controller.show();assert.equal(h.timers.size,0);
});
test('invalid settings fail closed',()=>{assert.throws(()=>create({}),TypeError);});
