import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, it, expect } from "vitest";
import { Tooltip, TooltipContent, TooltipProvider, TooltipTrigger } from "../Tooltip";

function renderTooltip(props: { content: string; side?: "top" | "bottom" | "left" | "right" }) {
  return render(
    <TooltipProvider delayDuration={0}>
      <Tooltip>
        <TooltipTrigger asChild>
          <button>Hover me</button>
        </TooltipTrigger>
        <TooltipContent side={props.side}>{props.content}</TooltipContent>
      </Tooltip>
    </TooltipProvider>,
  );
}

describe("Tooltip", () => {
  it("renders children", () => {
    renderTooltip({ content: "Tooltip content" });
    expect(screen.getByText("Hover me")).toBeInTheDocument();
  });

  it("shows tooltip content on hover", async () => {
    const user = userEvent.setup();
    renderTooltip({ content: "Tooltip content" });
    await user.hover(screen.getByText("Hover me"));
    expect(await screen.findByRole("tooltip")).toBeInTheDocument();
  });

  it("closes tooltip on trigger click", async () => {
    const user = userEvent.setup();
    renderTooltip({ content: "Tooltip content" });
    const trigger = screen.getByText("Hover me");
    await user.hover(trigger);
    await screen.findByRole("tooltip");
    await user.click(trigger);
    await waitFor(() => {
      expect(trigger.getAttribute("data-state")).toBe("closed");
    });
  });

  it("accepts position prop", () => {
    renderTooltip({ content: "Tooltip content", side: "bottom" });
    expect(screen.getByText("Hover me")).toBeInTheDocument();
  });
});
