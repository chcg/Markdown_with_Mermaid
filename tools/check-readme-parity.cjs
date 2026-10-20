// Structural/factual checks complement human review of the translation.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
function sections(file) {
  const text = fs.readFileSync(path.join(root,file),'utf8').replace(/\r\n/g,'\n');
  const chunks = text.split(/<!-- section:([a-z-]+) -->/);
  const result = new Map([['introduction',chunks[0]]]);
  for (let i=1;i<chunks.length;i+=2) {
    assert(!result.has(chunks[i]),'Duplicate README section: '+chunks[i]);
    result.set(chunks[i],chunks[i+1]);
  }
  assert.equal((text.match(/^## /gm)||[]).length,result.size-1,'Every section needs a heading and marker');
  return result;
}
const en = sections('README.md'), ru = sections('README.ru.md');
assert.deepEqual([...en.keys()],[...ru.keys()],'README section order differs');
const sorted = items => items.sort();
const links = text => sorted([...text.matchAll(/!?\[[^\]]*\]\(([^)]+)\)/g)].map(m=>m[1])
  .filter(x=>!/^README(?:\.ru)?\.md$/.test(x))
  // Language-specific screenshots show the same sample and diagram structure.
  .map(x=>x.replace(/^(docs\/screenshots\/preview-(?:light|dark))\.en(\.png)$/,'$1$2')));
const examples = text => [...text.matchAll(/```([^\n]*)\n([\s\S]*?)```/g)].map(m=>m[1]+'\n'+m[2].trim());
const inline = text => sorted([...text.replace(/```[\s\S]*?```/g,'').matchAll(/`([^`\n]+)`/g)].map(m=>m[1]));
const numbers = text => sorted((text.replace(/(\d)[ ,](?=\d{3}(?!\d))/g,'$1').replace(/(\d),(\d)/g,'$1.$2').match(/\d+(?:\.\d+)*%?/g)||[]));
const results=[];
for (const [id,english] of en) {
  const russian=ru.get(id);
  for (const [label,extract] of [['links',links],['code examples',examples],['technical identifiers',inline],['numbers and versions',numbers]]) {
    assert.deepEqual(extract(english),extract(russian),id+': '+label+' differ');
  }
  const items = text => (text.match(/^(?:- |\d+\. )/gm)||[]).length;
  assert.equal(items(english),items(russian),id+': list lengths differ');
  results.push(id);
}
console.log(JSON.stringify({status:'PASS',sections:results.length,checks:['section order','links and screenshot paths','code examples','technical identifiers','numeric facts','list lengths'],semanticReview:'requires human review; not automatically inferred',sectionIds:results},null,2));
