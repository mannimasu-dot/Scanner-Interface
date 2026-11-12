import fs from 'node:fs';
import path from 'node:path';
import { getDocument } from 'pdfjs-dist/legacy/build/pdf.mjs';
const root=path.dirname(new URL(import.meta.url).pathname.replace(/^\/(\w:)/,'$1'));
let failures=0;
const de=['Deutscher Scanner Integrationstest','Grüße aus München','Straße','Äpfel','Öl','4711','123 Euro'];
const en=['English scanner integration test','The quick brown fox jumps over the lazy dog.','8274','456 dollars'];
for(const [name,expected] of [['german.pdf',[de]],['english.pdf',[en]],['mixed-three-page-pdfa.pdf',[de,en,[]]],['blank-only-pdfa.pdf',[[]]]]) {
 try {
  const pdf=await getDocument({data:new Uint8Array(fs.readFileSync(path.join(root,name))),useSystemFonts:false}).promise;
  if(pdf.numPages!==expected.length) throw Error(`wrong page count ${pdf.numPages}`);
  for(let i=0;i<pdf.numPages;i++) {
   const page=await pdf.getPage(i+1); const content=await page.getTextContent(); const text=content.items.map(x=>x.str||'').join(' ').replace(/\s+/g,' ').trim();
   console.log(`${name} page ${i+1}: ${text||'[empty]'}`);
   for(const phrase of expected[i]) if(!text.includes(phrase)) { failures++; console.log(`FAIL missing ${JSON.stringify(phrase)}`); }
   if(!expected[i].length && text) { failures++; console.log('FAIL blank page contains text'); }
  }
  await pdf.destroy();
 } catch(e) { failures++; console.log(`FAIL ${name}: ${e.stack}`); }
}
console.log(`TOTAL FAILURES: ${failures}`); process.exitCode=failures?1:0;
