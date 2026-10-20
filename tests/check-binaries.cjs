const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const required = ['isUnicode','setInfo','getName','getFuncsArray','beNotified','messageProc'];
for (const [arch, machine] of [['x86',0x14c],['x64',0x8664]]) {
  const file = arch + '/Markdown_with_Mermaid/Markdown_with_Mermaid.dll';
  const b = fs.readFileSync(path.join(__dirname,'../dist',file));
  const pe = b.readUInt32LE(0x3c);
  assert.equal(b.toString('ascii',pe,pe+4),'PE\0\0');
  assert.equal(b.readUInt16LE(pe+4),machine);
  const loader = fs.readFileSync(path.join(__dirname,'../dist',arch,'Markdown_with_Mermaid',arch,'WebView2Loader.dll'));
  const loaderPe = loader.readUInt32LE(0x3c);
  assert.equal(loader.toString('ascii',loaderPe,loaderPe+4),'PE\0\0');
  assert.equal(loader.readUInt16LE(loaderPe+4),machine,'WebView2 loader architecture mismatch');
  const count = b.readUInt16LE(pe+6), optional = pe+24;
  const sectionBase = optional+b.readUInt16LE(pe+20);
  const sections = Array.from({length:count},(_,i)=>{
    const at=sectionBase+i*40;
    return {rva:b.readUInt32LE(at+12),size:Math.max(b.readUInt32LE(at+8),b.readUInt32LE(at+16)),raw:b.readUInt32LE(at+20)};
  });
  const offset = rva => { const s=sections.find(s=>rva>=s.rva && rva<s.rva+s.size); assert(s,`Unmapped RVA: ${rva}`); return s.raw+rva-s.rva; };
  const str = at => b.toString('ascii',at,b.indexOf(0,at));
  const directories = optional+(machine===0x8664?112:96);
  const exp = offset(b.readUInt32LE(directories));
  const names = offset(b.readUInt32LE(exp+32));
  const exports = Array.from({length:b.readUInt32LE(exp+24)},(_,i)=>str(offset(b.readUInt32LE(names+i*4))));
  required.forEach(name=>assert(exports.includes(name),`${file} lacks ${name}`));
  let imp=offset(b.readUInt32LE(directories+8)); const imports=[];
  while(b.readUInt32LE(imp+12)){imports.push(str(offset(b.readUInt32LE(imp+12))));imp+=20;}
  assert(!imports.some(n=>/vcruntime|msvcp|libstdc|libgcc|mscoree/i.test(n)), 'Unexpected compiler or .NET runtime dependency');
  console.log(`PASS ${file}: machine=${machine.toString(16)}, ${required.length} plugin exports; imports=${imports.join(', ')}`);
}
