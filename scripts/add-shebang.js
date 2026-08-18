import { readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';

const distIndex = path.join(process.cwd(), 'dist', 'index.js');
const shebang = '#!/usr/bin/env node\n';

async function addShebang() {
  const content = await readFile(distIndex, 'utf-8');

  if (!content.startsWith('#!')) {
    await writeFile(distIndex, shebang + content);
    console.log('Added shebang to dist/index.js');
  } else {
    console.log('Shebang already present');
  }
}

addShebang().catch(console.error);
