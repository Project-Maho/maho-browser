import unittest
try:
    import tomllib
except ImportError:
    import tomli as tomllib
from pathlib import Path

class TestWranglerTomlContract(unittest.TestCase):
    def setUp(self):
        self.toml_path = Path(__file__).parent.parent / "wrangler.toml"
        with open(self.toml_path, "rb") as f:
            self.config = tomllib.load(f)

    def test_staging_rate_limiter_namespaces(self):
        prod_env = self.config["env"]["production"]
        staging_env = self.config["env"]["staging"]

        prod_bindings = prod_env.get("unsafe", {}).get("bindings", [])
        staging_bindings = staging_env.get("unsafe", {}).get("bindings", [])

        prod_rl = {b["name"]: b["namespace_id"] for b in prod_bindings if b.get("type") == "ratelimit"}
        staging_rl = {b["name"]: b["namespace_id"] for b in staging_bindings if b.get("type") == "ratelimit"}

        # Require all 4 RL bindings
        expected_names = {"RL_SIGNUP", "RL_LOGIN", "RL_WAITLIST", "RL_REFRESH"}
        self.assertEqual(set(prod_rl.keys()), expected_names)
        self.assertEqual(set(staging_rl.keys()), expected_names)

        # Staging rate limit namespace IDs must be distinct from prod
        prod_ns = set(prod_rl.values())
        staging_ns = set(staging_rl.values())

        self.assertTrue(prod_ns.isdisjoint(staging_ns), f"Staging namespace IDs {staging_ns} overlap with production {prod_ns}")

        # Staging namespace IDs must be distinct from each other
        self.assertEqual(len(staging_rl.values()), len(staging_ns), f"Staging namespace IDs must be unique: {staging_rl.values()}")

        # Staging limits must remain 60
        for b in staging_bindings:
            if b.get("type") == "ratelimit":
                self.assertEqual(b.get("simple", {}).get("limit"), 60, f"{b['name']} limit in staging is not 60")
                self.assertEqual(b.get("simple", {}).get("period"), 60, f"{b['name']} period in staging is not 60")

    def test_staging_pro_variant(self):
        staging_vars = self.config["env"]["staging"]["vars"]
        self.assertEqual(staging_vars.get("LS_VARIANT_PRO"), "1970139", "Staging LS_VARIANT_PRO must equal 1970139")

if __name__ == "__main__":
    unittest.main()
