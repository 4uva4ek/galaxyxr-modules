import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
for (const suite of ['controller-suite','steamlink-suite']) {
 const base=`modules/${suite}/module/webroot/`;
 const html=fs.readFileSync(base+'index.html','utf8');
 new vm.Script(html.match(/<script>([\s\S]*?)<\/script>/)[1]);
 for (const [device,stored,expected] of [['en-US',null,'en'],['ru-RU',null,'ru'],['fr-FR',null,'en'],['en-US','ru','ru']]) {
  const nodes=[...html.matchAll(/data-i18n="([^"]+)"/g)].map(m=>({dataset:{i18n:m[1]},textContent:''}));
  const elements={language:{value:''},message:{textContent:''}};
  const context=vm.createContext({navigator:{language:'ru-RU'},document:{documentElement:{lang:''},querySelectorAll:()=>nodes,getElementById:id=>elements[id]},localStorage:{getItem:()=>stored,setItem:()=>{}},exec:async()=>({code:0,out:device}),refresh:()=>{}});
  vm.runInContext(fs.readFileSync(base+'i18n.js','utf8'),context);
  await vm.runInContext('initLocale()',context);
  assert.equal(context.document.documentElement.lang,expected);
  assert.equal(vm.runInContext("t('save')",context),expected==='ru'?'Сохранить':'Save');
  assert(nodes.every(n=>n.textContent && n.textContent!==n.dataset.i18n));
  elements.language.value=expected==='ru'?'en':'ru';elements.language.onchange();
  assert.notEqual(context.document.documentElement.lang,expected);
 }
 console.log('PASS '+suite+': system EN/RU, English fallback, saved override and manual switching; inline JS parses');
}
