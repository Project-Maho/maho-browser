describe('Maho Browser - Launch', () => {
    it('should launch the app', async () => {
        await browser.pause(3000);
        const source = await browser.getPageSource();
        expect(source).to.include('Maho');
    });

    it('should display the main view', async () => {
        const elements = await browser.$$('XCUIElementTypeStaticText');
        expect(elements.length).to.be.greaterThan(0);
    });
});
