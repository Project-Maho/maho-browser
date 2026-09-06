import {act, fireEvent, render, screen} from "@testing-library/react";
import {beforeEach, describe, expect, it, vi} from "vitest";
import {EmailBodyView} from "../EmailBodyView";
import {createMockAttachment, createMockEmailDetail, TestProviders} from "../../../test/mocks";

const wire = vi.hoisted(() => ({
  downloadAttachment: vi.fn(),
  saveAttachment: vi.fn(),
  openAttachment: vi.fn(),
}));
const notices = vi.hoisted(() => ({toast: vi.fn()}));
vi.mock("../../../mojo_client.js", () => ({handler: wire}));
vi.mock("../../../hooks/useSettings", () => ({
  useSettings: () => ({blockRemoteImages: true, blockTrackers: true, emailBodyFontSize: 14}),
}));
vi.mock("../../ui/Toast", () => ({useToast: () => notices}));

beforeEach(() => {
  vi.resetAllMocks();
  wire.downloadAttachment.mockResolvedValue({ok: true, resultJson: '"fixture-capability"'});
});

async function download() {
  render(<EmailBodyView emailDetail={createMockEmailDetail({
    attachments: [createMockAttachment({filename: "fixture.pdf"})],
  })} />, {wrapper: TestProviders});
  await act(async () => { fireEvent.click(screen.getByRole("button", {name: "Download"})); });
}

function noticeCount(type: string) {
  return notices.toast.mock.calls.filter(call => call[0] === type).length;
}

describe("attachment download persistence", () => {
  it("waits for the native save acknowledgement before reporting success", async () => {
    let finish: (result: {ok: boolean; errorMsg: string}) => void = () => {
      throw new Error("save completion not registered");
    };
    const pending = new Promise<{ok: boolean; errorMsg: string}>(resolve => { finish = resolve; });
    wire.saveAttachment.mockReturnValue(pending);

    await download();

    expect(wire.saveAttachment).toHaveBeenCalledExactlyOnceWith("fixture-capability");
    expect(noticeCount("success")).toBe(0);
    await act(async () => { finish({ok: true, errorMsg: ""}); await pending; });
    expect(noticeCount("success")).toBe(1);
    expect(noticeCount("error")).toBe(0);
    expect(wire.openAttachment).not.toHaveBeenCalled();
  });

  it("reports native save failure without reporting success", async () => {
    wire.saveAttachment.mockResolvedValue({ok: false, errorMsg: "fixture copy denied"});

    await download();

    expect(wire.saveAttachment).toHaveBeenCalledExactlyOnceWith("fixture-capability");
    expect(noticeCount("success")).toBe(0);
    expect(noticeCount("error")).toBe(1);
  });

  it("does not save or announce success when staging fails", async () => {
    wire.downloadAttachment.mockResolvedValue({ok: false, resultJson: "fixture unavailable"});

    await download();

    expect(wire.saveAttachment).not.toHaveBeenCalled();
    expect(wire.openAttachment).not.toHaveBeenCalled();
    expect(noticeCount("success")).toBe(0);
    expect(noticeCount("error")).toBe(1);
  });
});
