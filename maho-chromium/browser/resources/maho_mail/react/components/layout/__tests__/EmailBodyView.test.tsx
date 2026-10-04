import { fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { EmailBodyView } from "../EmailBodyView";
import { createMockEmailDetail, TestProviders } from "../../../test/mocks";

function loadBody(html: string, onEmailAddressClick = vi.fn()) {
  render(
    <EmailBodyView
      emailDetail={createMockEmailDetail({ email: { body_html: html } })}
      onEmailAddressClick={onEmailAddressClick}
    />,
    { wrapper: TestProviders },
  );
  const iframe = screen.getByTitle<HTMLIFrameElement>("Email body");
  const doc = iframe.contentDocument;
  if (!doc) throw new Error("Email body document is missing");
  // jsdom does not load srcDoc; materialize the actual component output and
  // dispatch its load event so the production listener handles every click.
  const parsed = new DOMParser().parseFromString(iframe.srcdoc, "text/html");
  doc.replaceChild(doc.importNode(parsed.documentElement, true), doc.documentElement);
  fireEvent.load(iframe);
  return doc;
}

afterEach(() => vi.restoreAllMocks());

describe("EmailBodyView link navigation", () => {
  it.each([
    ['<a href="https://example.test/path"><span id="target">Visit</span></a>', "click"],
    ['<a href="https://example.test/path"><span id="target">Visit</span></a>', "auxclick"],
    ['<svg><a href="https://example.test/path"><text id="target">Visit</text></a></svg>', "click"],
    ['<svg><a xlink:href="https://example.test/path"><text id="target">Visit</text></a></svg>', "click"],
  ])("opens external link outside the body frame: %s (%s)", (html, eventType) => {
    const open = vi.spyOn(window, "open").mockReturnValue(null);
    const doc = loadBody(html);
    const target = doc.getElementById("target");
    if (!target) throw new Error("Link target is missing");
    const event = new MouseEvent(eventType, {
      bubbles: true, cancelable: true, button: eventType === "auxclick" ? 1 : 0,
    });

    fireEvent(target, event);

    expect(event.defaultPrevented).toBe(true);
    expect(open).toHaveBeenCalledExactlyOnceWith(
      "https://example.test/path", "_blank", "noopener,noreferrer",
    );
  });

  it.each(["javascript:alert(1)", "file:///tmp/message", "chrome://settings/"])(
    "blocks a privileged or executable destination: %s", (href) => {
      const open = vi.spyOn(window, "open").mockReturnValue(null);
      const doc = loadBody('<a id="target">Visit</a>');
      const target = doc.getElementById("target");
      if (!target) throw new Error("Link target is missing");
      target.setAttribute("href", href);
      const event = new MouseEvent("click", { bubbles: true, cancelable: true });

      fireEvent(target, event);

      expect(event.defaultPrevented).toBe(true);
      expect(open).not.toHaveBeenCalled();
    },
  );

  it("keeps linkified addresses on the compose callback", () => {
    const onEmailAddressClick = vi.fn();
    const open = vi.spyOn(window, "open").mockReturnValue(null);
    const doc = loadBody("Contact alice@example.test", onEmailAddressClick);
    const target = doc.querySelector("a.maho-email-link");
    if (!target) throw new Error("Email address link is missing");

    fireEvent.click(target);

    expect(onEmailAddressClick).toHaveBeenCalledExactlyOnceWith("alice@example.test");
    expect(open).not.toHaveBeenCalled();
  });
});
