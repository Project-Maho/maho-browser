const fs = require('fs');
const path = require('path');

describe('Maho Browser - Visual Verification', () => {
    const screenshotDir = path.join(__dirname, '..', 'screenshots');
    
    before(async () => {
        if (!fs.existsSync(screenshotDir)) {
            fs.mkdirSync(screenshotDir, { recursive: true });
        }
    });

    it('should capture main screen', async () => {
        await browser.pause(2000);
        await browser.saveScreenshot(path.join(screenshotDir, 'main-screen.png'));
        expect(fs.existsSync(path.join(screenshotDir, 'main-screen.png'))).to.be.true;
    });

    it('should capture app source tree', async () => {
        const source = await browser.getPageSource();
        fs.writeFileSync(path.join(screenshotDir, 'source-tree.xml'), source);
        expect(source.length).to.be.greaterThan(0);
    });
});
