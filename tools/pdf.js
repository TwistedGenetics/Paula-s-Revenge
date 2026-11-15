const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage();
  await p.goto('file://' + process.argv[2], { waitUntil: 'load' });
  await p.evaluate(() => document.fonts.ready);
  // every picture loaded before printing (lazy ones too)
  await p.evaluate(async () => { const imgs = [...document.images]; imgs.forEach(i => i.loading = 'eager');
    await Promise.all(imgs.map(i => i.complete && i.naturalWidth ? 0 : new Promise(r => { i.onload = i.onerror = r; }))); });
  console.log('images', await p.evaluate(() => [...document.images].filter(i => i.naturalWidth > 0).length + ' of ' + document.images.length));
  await p.waitForTimeout(500);
  console.log('fonts', await p.evaluate(() => [...document.fonts].filter(f => f.status === 'loaded').map(f => f.family + ' ' + f.weight).join(', ')));
  await p.pdf({ path: process.argv[3], format: 'A4', printBackground: true, preferCSSPageSize: true,
    displayHeaderFooter: true, headerTemplate: '<span></span>',
    footerTemplate: '<div style="width:100%;font-family:Inter,Helvetica,Arial,sans-serif;font-weight:600;font-size:8pt;color:#4b5361;letter-spacing:.04em;text-align:center;padding-bottom:3mm;">' + (process.argv[4] || 'PAULA\'S REVENGE MANUAL') + ' &middot; PAGE <span class="pageNumber"></span> OF <span class="totalPages"></span></div>' });
  await b.close();
})();
