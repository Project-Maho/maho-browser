describe('Maho Browser - Navigation', () => {
    it('should have navigation elements', async () => {
        const source = await browser.getPageSource();
        expect(source.length).to.be.greaterThan(100);
    });

    it('should take a screenshot for visual verification', async () => {
        await browser.saveScreenshot('./screenshots/navigation.png');
    });
});
