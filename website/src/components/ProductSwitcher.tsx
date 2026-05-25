type Product = "browser" | "mail";

interface ProductSwitcherProps {
  activeProduct: Product;
}

export function ProductSwitcher({ activeProduct }: ProductSwitcherProps) {
  const handleSwitch = (nextProduct: Product) => {
    if (nextProduct === activeProduct) return;
    
    if (nextProduct === "browser") {
      window.location.href = "/";
    } else {
      window.location.href = "/mail";
    }
  };

  return (
    <div
      className="inline-flex items-center rounded-full border border-border bg-card p-1"
      role="tablist"
      aria-label="Product switcher"
    >
      {(["browser", "mail"] as const).map((item) => {
        const active = activeProduct === item;
        return (
          <button
            key={item}
            type="button"
            role="tab"
            aria-selected={active}
            onClick={() => handleSwitch(item)}
            className={[
              "rounded-full px-3 py-1.5 text-sm font-medium transition-colors",
              active
                ? "bg-foreground text-background"
                : "text-muted-foreground hover:text-foreground",
            ].join(" ")}
          >
            {item === "browser" ? "Browser" : "Mail"}
          </button>
        );
      })}
    </div>
  );
}

export default ProductSwitcher;
