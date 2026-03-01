/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

(function () {
  const byId = (id) => document.getElementById(id);
  const log = (...args) => console.log("[web-ui]", ...args);
  const withDefault = (value, fallback) => (value === null || value === undefined ? fallback : value);
  let joinStatusPollTimer = null;
  let deviceListPollTimer = null;
  let forceRemoveIndicatorTimer = null;
  let joinSubmitInFlight = false;
  let devicesLoadInFlight = false;
  let scanInFlight = false;
  let pendingRemoveRequest = null;
  let statusToastTimer = null;

  const ui = {
    statusLine: byId("status-line"),
    toastMessage: byId("toast-message"),
    refreshAllBtn: byId("refresh-all-btn"),
    networkConnected: byId("network-connected"),
    networkRevision: byId("network-revision"),
    networkRefreshBtn: byId("network-refresh-btn"),
    networkScanBtn: byId("network-scan-btn"),
    wifiCredentialsStatus: byId("wifi-credentials-status"),
    wifiCredentialsSsid: byId("wifi-credentials-ssid"),
    wifiScanBody: byId("wifi-scan-body"),
    wifiConnectForm: byId("wifi-connect-form"),
    wifiSsid: byId("wifi-ssid"),
    wifiPassword: byId("wifi-password"),
    wifiSaveCredentials: byId("wifi-save-credentials"),
    deviceJoinForm: byId("device-join-form"),
    deviceJoinDuration: byId("device-join-duration"),
    deviceJoinBtn: byId("device-join-btn"),
    devicesBody: byId("devices-body"),
    configForm: byId("config-form"),
    configTimeout: byId("config-timeout"),
    configRetries: byId("config-retries"),
    configLastStatus: byId("config-last-status"),
    configRevision: byId("config-revision"),
    removeConfirmBox: byId("remove-confirm-box"),
    removeConfirmText: byId("remove-confirm-text"),
    removeForceRow: byId("remove-force-row"),
    removeForceCheckbox: byId("remove-force-checkbox"),
    removeConfirmBtn: byId("remove-confirm-btn"),
    removeCancelBtn: byId("remove-cancel-btn"),
  };

  function bind(element, eventName, handler) {
    if (!element) {
      log("Missing UI element for event:", eventName);
      return;
    }
    element.addEventListener(eventName, handler);
  }

  function setStatus(message, level) {
    log("status:", message, level || "info");
    ui.statusLine.textContent = message;
    ui.statusLine.className = "status-line";
    if (level) {
      ui.statusLine.classList.add("status-" + level);
    }

    if (!ui.toastMessage) {
      return;
    }

    ui.toastMessage.textContent = message;
    ui.toastMessage.className = "toast-message";
    if (level) {
      ui.toastMessage.classList.add("status-" + level);
    }
    ui.toastMessage.hidden = false;
    if (statusToastTimer !== null) {
      window.clearTimeout(statusToastTimer);
    }
    statusToastTimer = window.setTimeout(function () {
      if (ui.toastMessage) {
        ui.toastMessage.hidden = true;
      }
      statusToastTimer = null;
    }, 3500);
  }

  window.addEventListener("error", function (event) {
    log("window error:", event.message, "at", event.filename + ":" + event.lineno);
  });

  window.addEventListener("unhandledrejection", function (event) {
    log("unhandled rejection:", event.reason);
  });

  async function requestJson(url, options) {
    log("request:start", url, options && options.method ? options.method : "GET");
    const response = await fetch(url, options);
    const text = await response.text();
    log("request:response", url, response.status, text);
    let payload = {};
    if (text) {
      try {
        payload = JSON.parse(text);
      } catch (_error) {
        payload = { error: "invalid_json" };
      }
    }

    if (!response.ok) {
      const reason = payload && payload.error ? payload.error : "request_failed";
      const error = new Error(reason);
      error.status = response.status;
      error.payload = payload;
      throw error;
    }

    return payload;
  }

  function sleep(ms) {
    return new Promise(function (resolve) {
      window.setTimeout(resolve, ms);
    });
  }

  function extractRequestId(payload) {
    const requestId = Number(withDefault(payload && payload.request_id, payload && payload.correlation_id));
    if (!Number.isFinite(requestId) || requestId <= 0) {
      throw new Error("invalid_request_id");
    }
    return requestId;
  }

  async function pollNetworkResult(requestId, timeoutMs, pollIntervalMs, onPending) {
    const startedAt = Date.now();
    const timeout = Number.isFinite(timeoutMs) && timeoutMs > 0 ? timeoutMs : 5000;
    const interval = Number.isFinite(pollIntervalMs) && pollIntervalMs > 0 ? pollIntervalMs : 150;

    while (Date.now() - startedAt <= timeout) {
      const result = await requestJson("/api/network/result?request_id=" + String(requestId), { method: "GET" });
      if (Boolean(result.ready)) {
        return result;
      }
      if (typeof onPending === "function") {
        onPending(result);
      }
      await sleep(interval);
    }

    throw new Error("operation_timeout");
  }

  function renderDevices(data) {
    const devices = Array.isArray(data.devices) ? data.devices : [];
    if (devices.length === 0) {
      ui.devicesBody.innerHTML = '<tr><td colspan="4">No devices</td></tr>';
      return;
    }

    const rows = devices
      .map((device) => {
        const shortAddr = Number(device.short_addr || 0);
        const powerOn = Boolean(device.power_on);
        const forceRemoveArmed = Boolean(device.force_remove_armed);
        const forceRemoveMsLeft = Number(withDefault(device.force_remove_ms_left, 0));
        const forceRemoveSecondsLeft = Math.max(0, Math.ceil(forceRemoveMsLeft / 1000));
        const forceRemoveIndicator = forceRemoveArmed
          ? '<div class="pending-remove" data-force-remove-seconds="' +
            String(forceRemoveSecondsLeft) +
            '">force-remove armed (' +
            String(forceRemoveSecondsLeft) +
            "s left)</div>"
          : "";
        const removeDisabledAttr = forceRemoveArmed ? " disabled" : "";
        return (
          "<tr>" +
          "<td>" +
          shortAddr +
          "</td>" +
          "<td>" +
          (device.online ? "yes" : "no") +
          "</td>" +
          "<td>" +
          (powerOn ? "on" : "off") +
          "</td>" +
          '<td><button class="secondary" data-device-toggle="' +
          shortAddr +
          '" data-next-power="true">Turn On</button> <button class="secondary" data-device-toggle="' +
          shortAddr +
          '" data-next-power="false">Turn Off</button> <button class="danger" data-device-remove="' +
          shortAddr +
          '"' +
          removeDisabledAttr +
          ">Remove</button>" +
          forceRemoveIndicator +
          "</td>" +
          "</tr>"
        );
      })
      .join("");

    ui.devicesBody.innerHTML = rows;
    ensureForceRemoveIndicatorTimer();
  }

  function stopJoinStatusPolling() {
    if (joinStatusPollTimer !== null) {
      window.clearInterval(joinStatusPollTimer);
      joinStatusPollTimer = null;
    }
  }

  function ensureDeviceListPolling() {
    if (deviceListPollTimer !== null) {
      return;
    }
    deviceListPollTimer = window.setInterval(function () {
      if (scanInFlight) {
        return;
      }
      loadDevices().catch(function (error) {
        log("device polling loadDevices failed:", error && error.message ? error.message : error);
      });
    }, 2000);
  }

  function applyJoinWindowState(open, secondsLeft) {
    const joinOpen = Boolean(open);
    const safeSeconds = Math.max(0, Number(withDefault(secondsLeft, 0)));

    if (joinOpen) {
      ui.deviceJoinBtn.classList.add("joining");
      ui.deviceJoinBtn.disabled = true;
      ui.deviceJoinBtn.style.setProperty("--join-progress", "0%");
      ui.deviceJoinBtn.textContent = "Joining... " + String(safeSeconds) + "s";
      if (joinStatusPollTimer === null) {
        joinStatusPollTimer = window.setInterval(function () {
          loadDevices().catch(function (error) {
            log("join polling loadDevices failed:", error && error.message ? error.message : error);
          });
        }, 1000);
      }
      return;
    }

    stopJoinStatusPolling();
    ui.deviceJoinBtn.classList.remove("joining");
    ui.deviceJoinBtn.disabled = false;
    ui.deviceJoinBtn.style.removeProperty("--join-progress");
    ui.deviceJoinBtn.textContent = "Join Device";
  }

  async function loadDevices() {
    if (devicesLoadInFlight) {
      return;
    }
    devicesLoadInFlight = true;
    log("loadDevices");
    try {
      const data = await requestJson("/api/devices", { method: "GET" });
      renderDevices(data);
      applyJoinWindowState(data.join_window_open, data.join_window_seconds_left);
    } finally {
      devicesLoadInFlight = false;
    }
  }

  async function loadNetwork() {
    log("loadNetwork");
    const data = await requestJson("/api/network", { method: "GET" });
    ui.networkConnected.textContent = data.connected ? "yes" : "no";
    ui.networkRevision.textContent = String(withDefault(data.revision, "-"));
  }

  async function loadCredentialsStatus() {
    log("loadCredentialsStatus");
    const accepted = await requestJson("/api/network/credentials/status", { method: "GET" });
    const requestId = extractRequestId(accepted);
    const data = await pollNetworkResult(requestId, 2000, 100);
    const saved = Boolean(data.saved);
    const hasPassword = Boolean(data.has_password);
    ui.wifiCredentialsStatus.textContent = saved
      ? hasPassword
        ? "saved"
        : "saved (open/no password)"
      : "not saved";
    ui.wifiCredentialsSsid.textContent = saved ? String(data.ssid || "-") : "-";
  }

  function renderScanResult(data) {
    const networks = Array.isArray(data.networks) ? data.networks : [];
    if (networks.length === 0) {
      ui.wifiScanBody.innerHTML = '<tr><td colspan="3">No networks found</td></tr>';
      return;
    }

    ui.wifiScanBody.innerHTML = networks
      .map(function (net) {
        const ssid = String(net.ssid || "");
        const safeSsid = ssid.replace(/"/g, "&quot;");
        return (
          "<tr data-ssid=\"" +
          safeSsid +
          "\">" +
          "<td>" +
          ssid +
          "</td>" +
          "<td>" +
              String(withDefault(net.rssi, "-")) +
              "</td>" +
              "<td>" +
              (Boolean(withDefault(net.is_open, net.open)) ? "yes" : "no") +
              "</td>" +
              "</tr>"
            );
      })
      .join("");
  }

  async function loadConfig() {
    log("loadConfig");
    const data = await requestJson("/api/config", { method: "GET" });
    ui.configTimeout.value = String(withDefault(data.command_timeout_ms, ""));
    ui.configRetries.value = String(withDefault(data.max_command_retries, ""));
    ui.configLastStatus.textContent = String(withDefault(data.last_command_status, "-"));
    ui.configRevision.textContent = String(withDefault(data.revision, "-"));
  }

  async function refreshAll() {
    log("refreshAll");
    setStatus("Refreshing...", "warn");
    try {
      await Promise.all([loadDevices(), loadNetwork(), loadCredentialsStatus(), loadConfig()]);
      setStatus("Data updated", "ok");
    } catch (error) {
      setStatus("Refresh failed: " + error.message, "error");
    }
  }

  async function submitNetworkRefresh() {
    log("submitNetworkRefresh");
    setStatus("Submitting network refresh...", "warn");
    try {
      const result = await requestJson("/api/network/refresh", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: "{}",
      });
      setStatus("Network refresh accepted, correlation_id=" + result.correlation_id, "ok");
      await loadNetwork();
    } catch (error) {
      setStatus("Network refresh failed: " + error.message, "error");
    }
  }

  async function scanNetworks() {
    if (scanInFlight) {
      setStatus("Scan is already in progress...", "warn");
      return;
    }
    scanInFlight = true;
    if (ui.networkScanBtn) {
      ui.networkScanBtn.disabled = true;
    }
    log("scanNetworks:button-click");
    setStatus("Scanning Wi-Fi...", "warn");
    try {
      const accepted = await requestJson("/api/network/scan", { method: "GET" });
      const requestId = extractRequestId(accepted);
      let scanQueuedShown = false;
      let scanProgressShown = false;
      const data = await pollNetworkResult(requestId, 15000, 200, function (pending) {
        if (pending && pending.status === "scan_queued" && !scanQueuedShown) {
          scanQueuedShown = true;
          setStatus("Scanning Wi-Fi... (queued)", "warn");
          return;
        }
        if (pending && pending.status === "scan_in_progress" && !scanProgressShown) {
          scanProgressShown = true;
          setStatus("Scanning Wi-Fi... (scan in progress)", "warn");
        }
      });
      if (!Boolean(data.ok)) {
        throw new Error(String(withDefault(data.error, "scan_failed")));
      }
      renderScanResult(data);
      const count = Number(
        withDefault(data.count, withDefault(data.scan_count, Array.isArray(data.networks) ? data.networks.length : 0))
      );
      setStatus("Scan complete: " + String(Number.isFinite(count) ? count : 0) + " network(s)", "ok");
    } catch (error) {
      setStatus("Scan failed: " + error.message, "error");
    } finally {
      scanInFlight = false;
      if (ui.networkScanBtn) {
        ui.networkScanBtn.disabled = false;
      }
    }
  }

  async function submitWifiConnect(event) {
    event.preventDefault();
    log("submitWifiConnect");

    const ssid = ui.wifiSsid.value.trim();
    if (!ssid) {
      setStatus("SSID is required", "error");
      return;
    }

    const password = ui.wifiPassword.value;
    const saveCredentials = Boolean(ui.wifiSaveCredentials.checked);

    setStatus("Connecting to Wi-Fi...", "warn");
    try {
      const accepted = await requestJson("/api/network/connect", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          ssid: ssid,
          password: password,
          save_credentials: saveCredentials,
        }),
      });
      const requestId = extractRequestId(accepted);
      const result = await pollNetworkResult(requestId, 15000, 200);
      if (!Boolean(result.ok)) {
        throw new Error(String(withDefault(result.error, "connect_failed")));
      }
      setStatus("Wi-Fi connect request accepted", "ok");
      await loadNetwork();
      await loadCredentialsStatus();
    } catch (error) {
      setStatus("Wi-Fi connect failed: " + error.message, "error");
    }
  }

  async function submitDevicePower(shortAddr, nextPower) {
    log("submitDevicePower", shortAddr, nextPower);
    setStatus("Submitting device command...", "warn");
    try {
      const result = await requestJson("/api/devices/power", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          short_addr: shortAddr,
          power_on: nextPower,
        }),
      });
      setStatus("Device command accepted, correlation_id=" + result.correlation_id, "ok");
      await loadDevices();
      await loadConfig();
    } catch (error) {
      setStatus("Device command failed: " + error.message, "error");
    }
  }

  function removeDeviceRowFromUi(shortAddr) {
    const row = ui.devicesBody.querySelector('button[data-device-remove="' + String(shortAddr) + '"]');
    if (!row) {
      return;
    }
    const tableRow = row.closest("tr");
    if (tableRow) {
      tableRow.remove();
    }
    if (!ui.devicesBody.querySelector("tr")) {
      ui.devicesBody.innerHTML = '<tr><td colspan="4">No devices</td></tr>';
    }
  }

  function hideRemoveConfirm() {
    pendingRemoveRequest = null;
    ui.removeForceCheckbox.checked = false;
    ui.removeConfirmBox.hidden = true;
  }

  function requestDeviceRemove(shortAddr) {
    const hex = "0x" + shortAddr.toString(16);
    pendingRemoveRequest = { shortAddr: shortAddr };
    ui.removeConfirmText.textContent = "Confirm device removal " + hex;
    ui.removeConfirmBox.hidden = false;
    setStatus("Confirm removal in the Status section", "warn");
  }

  async function submitDeviceRemove(shortAddr, forceRemove) {
    log("submitDeviceRemove", shortAddr, forceRemove);
    const forceRemoveTimeoutMs = 5000;

    setStatus("Removing device...", "warn");
    try {
      const accepted = await requestJson("/api/devices/remove", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          short_addr: shortAddr,
          force_remove: forceRemove,
          force_remove_timeout_ms: forceRemoveTimeoutMs,
        }),
      });
      const requestId = extractRequestId(accepted);
      const result = await pollNetworkResult(requestId, 8000, 200);
      if (!Boolean(result.ok)) {
        throw new Error(String(withDefault(result.error, "remove_failed")));
      }
      if (result.force_remove) {
        setStatus(
          "Force remove armed, fallback timeout " +
            String(withDefault(result.force_remove_timeout_ms, forceRemoveTimeoutMs)) +
            " ms",
          "ok"
        );
        await loadDevices();
      } else {
        removeDeviceRowFromUi(shortAddr);
        setStatus("Device remove requested", "ok");
      }
      await loadConfig();
      window.setTimeout(refreshAll, 1500);
    } catch (error) {
      setStatus("Device remove failed: " + error.message, "error");
    }
  }

  async function submitDeviceJoin() {
    log("submitDeviceJoin");
    if (joinSubmitInFlight) {
      return;
    }

    const durationSeconds = Number(ui.deviceJoinDuration.value);
    if (!Number.isFinite(durationSeconds) || durationSeconds < 1 || durationSeconds > 255) {
      setStatus("Join duration must be in range 1..255 seconds", "error");
      return;
    }

    joinSubmitInFlight = true;
    ui.deviceJoinBtn.disabled = true;
    setStatus("Starting join window...", "warn");
    try {
      const accepted = await requestJson("/api/devices/join", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ duration_seconds: durationSeconds }),
      });
      const requestId = extractRequestId(accepted);
      const result = await pollNetworkResult(requestId, 5000, 150);
      if (!Boolean(result.ok)) {
        throw new Error(String(withDefault(result.error, "join_failed")));
      }
      const activeWindow = Number(withDefault(result.duration_seconds, durationSeconds));
      setStatus("Join window opened for " + String(activeWindow) + "s", "ok");
      await loadDevices();
    } catch (error) {
      if (error && error.message === "join_window_already_open") {
        const secondsLeft = Number(withDefault(error.payload && error.payload.seconds_left, 0));
        setStatus("Join window already open (" + String(Math.max(0, secondsLeft)) + "s left)", "warn");
        await loadDevices();
        return;
      }
      setStatus("Device join failed: " + error.message, "error");
    } finally {
      joinSubmitInFlight = false;
      if (!ui.deviceJoinBtn.classList.contains("joining")) {
        ui.deviceJoinBtn.disabled = false;
      }
    }
  }

  function tickForceRemoveIndicators() {
    const indicators = ui.devicesBody.querySelectorAll(".pending-remove[data-force-remove-seconds]");
    indicators.forEach(function (node) {
      const current = Number(node.getAttribute("data-force-remove-seconds"));
      if (!Number.isFinite(current) || current <= 0) {
        return;
      }
      const next = current - 1;
      node.setAttribute("data-force-remove-seconds", String(next));
      node.textContent = "force-remove armed (" + String(next) + "s left)";
    });
  }

  function ensureForceRemoveIndicatorTimer() {
    if (forceRemoveIndicatorTimer !== null) {
      return;
    }
    forceRemoveIndicatorTimer = window.setInterval(tickForceRemoveIndicators, 1000);
  }

  async function submitConfig(event) {
    event.preventDefault();
    log("submitConfig");

    const timeout = Number(ui.configTimeout.value);
    const retries = Number(ui.configRetries.value);
    const payload = {};
    if (Number.isFinite(timeout) && timeout > 0) {
      payload.command_timeout_ms = timeout;
    }
    if (Number.isFinite(retries) && retries >= 0 && retries <= 5) {
      payload.max_command_retries = retries;
    }

    setStatus("Saving config...", "warn");
    try {
      await requestJson("/api/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });
      setStatus("Config update accepted", "ok");
      await loadConfig();
    } catch (error) {
      setStatus("Config save failed: " + error.message, "error");
    }
  }

  bind(ui.refreshAllBtn, "click", refreshAll);
  bind(ui.networkRefreshBtn, "click", submitNetworkRefresh);
  bind(ui.networkScanBtn, "click", scanNetworks);
  bind(ui.wifiConnectForm, "submit", submitWifiConnect);
  bind(ui.deviceJoinForm, "submit", function (event) {
    event.preventDefault();
  });
  bind(ui.deviceJoinBtn, "click", submitDeviceJoin);
  bind(ui.configForm, "submit", submitConfig);
  bind(ui.removeConfirmBtn, "click", function () {
    if (!pendingRemoveRequest) {
      return;
    }
    const request = pendingRemoveRequest;
    const forceRemove = Boolean(ui.removeForceCheckbox.checked);
    hideRemoveConfirm();
    submitDeviceRemove(request.shortAddr, forceRemove);
  });
  bind(ui.removeCancelBtn, "click", function () {
    hideRemoveConfirm();
    setStatus("Removal canceled", "warn");
  });
  bind(ui.wifiScanBody, "click", function (event) {
    const row = event.target.closest("tr");
    if (!row) {
      return;
    }
    const ssid = row.getAttribute("data-ssid");
    if (!ssid) {
      return;
    }
    ui.wifiSsid.value = ssid;
  });
  bind(ui.devicesBody, "click", function (event) {
    const target = event.target;
    if (!(target instanceof HTMLButtonElement)) {
      return;
    }
    const removeText = target.getAttribute("data-device-remove");
    if (removeText) {
      const shortAddrRemove = Number(removeText);
      if (!Number.isFinite(shortAddrRemove)) {
        return;
      }
      requestDeviceRemove(shortAddrRemove);
      return;
    }
    const addrText = target.getAttribute("data-device-toggle");
    const nextPowerText = target.getAttribute("data-next-power");
    if (!addrText || !nextPowerText) {
      return;
    }
    const shortAddr = Number(addrText);
    const nextPower = nextPowerText === "true";
    if (!Number.isFinite(shortAddr)) {
      return;
    }
    log("device-toggle click", shortAddr, nextPower ? "ON" : "OFF");
    submitDevicePower(shortAddr, nextPower);
  });

  log("ui initialized");
  ensureDeviceListPolling();
  refreshAll();
})();
