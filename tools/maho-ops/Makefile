.PHONY: setup dev release-check logs install-service start-service stop-service uninstall-service labels test-discord test

VENV := .venv
PY := $(VENV)/bin/python
PLIST := $(HOME)/Library/LaunchAgents/ai.maho.ops.plist

setup:
	uv venv --python 3.11 $(VENV)
	uv pip install --python $(PY) -r requirements.txt

dev:
	$(PY) bot.py

release-check:
	$(PY) release_watcher.py

test-discord:
	@bash -c 'set -a; source .env; set +a; curl -s -H "Authorization: Bot $$DISCORD_BOT_TOKEN" https://discord.com/api/v10/users/@me | python3 -m json.tool'

labels:
	@bash scripts/setup_labels.sh

install-service:
	cp scripts/ai.maho.ops.plist $(PLIST)
	launchctl load $(PLIST)
	@echo "Service installed. Use 'make start-service' or wait for KeepAlive to kick in."

start-service:
	launchctl start ai.maho.ops

stop-service:
	launchctl stop ai.maho.ops

uninstall-service:
	launchctl unload $(PLIST) || true
	rm -f $(PLIST)

logs:
	tail -f logs/bot.log

clean:
	rm -rf $(VENV) __pycache__ *.pyc
