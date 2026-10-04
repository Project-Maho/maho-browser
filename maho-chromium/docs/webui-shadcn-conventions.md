# Maho WebUI: shadcn + Tailwind v4 Conventions

This document outlines the styling and UI design patterns for Maho Browser WebUI surfaces.

## Key Technologies
1. **Tailwind CSS v4:** Modern styling system utilizing CSS variables and modern `@theme` syntax.
2. **shadcn/ui (React):** Reusable component layout, backed by `@radix-ui` primitives for accessible UI behaviors (focus traps, select menus, toggle groups).
3. **lucide-react:** Re-exported selected icons under `@icons/lucide` for unified tree-shakeable icons.

## File Hierarchy
```
maho_common/react/
├── ui/                  # Component Library
│   ├── button.tsx
│   ├── input.tsx
│   ├── select.tsx
│   └── ...
├── lib/
│   └── utils.ts         # cn() helper
├── theme/
│   ├── tokens.css       # central @theme definition and default dual-theme
│   └── apply_theme.ts   # sets html[data-theme] attribute
└── icons/
    └── lucide.ts        # re-export selected Lucide icons
```

## Adding a Component
To add a new shadcn component:
1. Author it under `maho_common/react/ui/` using typescript and Tailwind utility classes.
2. If it wraps a Radix primitive, ensure types are mapped in both `maho_ai/react/tsconfig.json` and `maho_settings/react/tsconfig.json`.
3. Add the source file path to the `sources` list inside the `tailwind_css("build_tailwind")` block in each consuming feature's `BUILD.gn` (e.g. `maho_ai/BUILD.gn` and `maho_settings/BUILD.gn`).
4. Document the component in `maho_common/components.json`.

## Customization Guidelines
- Use curated colors from the **zinc** scale for neutral backgrounds/elements.
- Access theme variables dynamically (`bg-background`, `text-foreground`, `border-border`).
- Toggle styles depending on state using prefix modifiers (e.g. `data-[state=checked]:bg-primary`).
- Restrict custom CSS override files by writing tailwind utility classes directly in JSX.
