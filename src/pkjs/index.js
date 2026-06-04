/**
 * Solus_v2 Watchface — PebbleKit JS (src/pkjs/index.js)
 *
 * Responsibilities:
 *   1. Fetch real sunrise/sunset times from the Sunrise-Sunset API and send
 *      them to the watch whenever the watch requests it.
 *   2. Serve the configuration page and forward the animation-mode setting to
 *      the watch via AppMessage.
 */

'use strict';

// ─── AppMessage key integers ──────────────────────────────────────────────────
// CloudPebble "Automatic assignment" assigns integers alphabetically by key name.
// The declared names are KEY_ANIM_MODE, KEY_SUNRISE, KEY_SUNSET, so:
//   KEY_ANIM_MODE = 0   (A before S)
//   KEY_SUNRISE   = 1
//   KEY_SUNSET    = 2
// These must stay in sync with the MESSAGE_KEY_KEY_* constants the SDK generates
// in the C header.  If you add or rename a key in CloudPebble Settings, re-check
// the alphabetical order and update both this file and the C switch statement.
var KEY_ANIM_MODE = 0;
var KEY_SUNRISE   = 1;
var KEY_SUNSET    = 2;

// ─── Configuration page HTML (embedded as a data URI) ────────────────────────
// The page is self-contained: no external CDN required, no server needed.
var CONFIG_PAGE_HTML = [
  '<!DOCTYPE html>',
  '<html lang="en">',
  '<head>',
  '  <meta charset="UTF-8">',
  '  <meta name="viewport" content="width=device-width, initial-scale=1">',
  '  <title>Solus Settings</title>',
  '  <style>',
  '    * { box-sizing: border-box; margin: 0; padding: 0; }',
  '    body {',
  '      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;',
  '      background: #1a1a2e; color: #eee;',
  '      min-height: 100vh; display: flex; flex-direction: column;',
  '    }',
  '    header {',
  '      background: #16213e;',
  '      padding: 16px 20px;',
  '      text-align: center;',
  '      border-bottom: 1px solid #0f3460;',
  '    }',
  '    header h1 { font-size: 1.4em; color: #e94560; letter-spacing: 2px; }',
  '    header p  { font-size: 0.8em; color: #999; margin-top: 4px; }',
  '    main { flex: 1; padding: 24px 20px; }',
  '    .section-title {',
  '      font-size: 0.75em; text-transform: uppercase; letter-spacing: 1.5px;',
  '      color: #e94560; margin-bottom: 12px;',
  '    }',
  '    .card {',
  '      background: #16213e; border-radius: 12px;',
  '      overflow: hidden; margin-bottom: 24px;',
  '    }',
  '    .option {',
  '      display: flex; align-items: center;',
  '      padding: 16px 18px; cursor: pointer;',
  '      border-bottom: 1px solid #0f3460; transition: background 0.15s;',
  '    }',
  '    .option:last-child { border-bottom: none; }',
  '    .option:hover { background: #0f3460; }',
  '    .option input[type="radio"] { display: none; }',
  '    .radio-dot {',
  '      width: 20px; height: 20px; border-radius: 50%;',
  '      border: 2px solid #555; margin-right: 14px; flex-shrink: 0;',
  '      display: flex; align-items: center; justify-content: center;',
  '      transition: border-color 0.15s;',
  '    }',
  '    .option.selected .radio-dot {',
  '      border-color: #e94560;',
  '      background: radial-gradient(circle, #e94560 40%, transparent 40%);',
  '    }',
  '    .option-text strong { display: block; font-size: 0.95em; }',
  '    .option-text small  { color: #999; font-size: 0.78em; margin-top: 2px; display: block; }',
  '    footer { padding: 0 20px 32px; }',
  '    button {',
  '      width: 100%; padding: 14px; border-radius: 12px;',
  '      background: #e94560; color: #fff; border: none;',
  '      font-size: 1em; font-weight: 600; cursor: pointer;',
  '      letter-spacing: 0.5px; transition: opacity 0.15s;',
  '    }',
  '    button:hover { opacity: 0.85; }',
  '  </style>',
  '</head>',
  '<body>',
  '  <header>',
  '    <h1>✦ SOLUS_V2</h1>',
  '    <p>Watchface Settings</p>',
  '  </header>',
  '  <main>',
  '    <div class="section-title">Minute Animation</div>',
  '    <div class="card" id="options">',
  '      <label class="option" data-value="0">',
  '        <input type="radio" name="anim" value="0">',
  '        <span class="radio-dot"></span>',
  '        <span class="option-text">',
  '          <strong>Always On</strong>',
  '          <small>Planets orbit at every minute tick.</small>',
  '        </span>',
  '      </label>',
  '      <label class="option" data-value="1">',
  '        <input type="radio" name="anim" value="1">',
  '        <span class="radio-dot"></span>',
  '        <span class="option-text">',
  '          <strong>Daytime Only</strong>',
  '          <small>Animations play only between sunrise and sunset.</small>',
  '        </span>',
  '      </label>',
  '      <label class="option" data-value="2">',
  '        <input type="radio" name="anim" value="2">',
  '        <span class="radio-dot"></span>',
  '        <span class="option-text">',
  '          <strong>Always Off</strong>',
  '          <small>Planets jump to their positions with no animation.</small>',
  '        </span>',
  '      </label>',
  '    </div>',
  '  </main>',
  '  <footer>',
  '    <button id="save">Save &amp; Close</button>',
  '  </footer>',
  '  <script>',
  '    // Current mode is injected by the JS layer as a numeric literal.',
  '    // (Passing it via data: URI hash does not work — webviews drop the fragment.)',
  '    var current = __CURRENT_MODE__;',
  '    // Highlight the currently-selected option.',
  '    function selectOption(value) {',
  '      current = value;',
  '      document.querySelectorAll(".option").forEach(function(el) {',
  '        el.classList.toggle("selected", +el.dataset.value === value);',
  '      });',
  '    }',
  '    selectOption(current);',
  '    // Wire up click handlers.',
  '    document.querySelectorAll(".option").forEach(function(el) {',
  '      el.addEventListener("click", function() {',
  '        selectOption(+el.dataset.value);',
  '      });',
  '    });',
  '    // Save button — return the result to Pebble via the SDK 3 close scheme.',
  '    // pebblejs://close# is the correct SDK 3 URI; pebblekit://close?result= is',
  '    // the deprecated SDK 2 form and is not recognised by current firmware.',
  '    document.getElementById("save").addEventListener("click", function() {',
  '      var result = encodeURIComponent(JSON.stringify({ animMode: current }));',
  '      document.location = "pebblejs://close#" + result;',
  '    });',
  '  </script>',
  '</body>',
  '</html>',
].join('\n');

// ─── Utility: simple XMLHttpRequest wrapper ────────────────────────────────

function xhrGet(url, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    // Treat any non-2xx response as an error so rate-limits and server errors
    // don't get silently parsed as valid daylight data.
    if (this.status >= 200 && this.status < 300) {
      callback(null, this.responseText);
    } else {
      callback(new Error('HTTP ' + this.status));
    }
  };
  xhr.onerror = function() { callback(new Error('XHR error')); };
  xhr.open('GET', url, true);
  xhr.send();
}

// ─── Daylight data ─────────────────────────────────────────────────────────

function requestDaylightData(position) {
  var lat = position.coords.latitude;
  var lng = position.coords.longitude;
  var url = 'https://api.sunrise-sunset.org/json?date=today&formatted=0'
          + '&lat=' + lat + '&lng=' + lng;

  console.log('[Solus] Fetching daylight data: ' + url);

  xhrGet(url, function(err, responseText) {
    if (err) {
      console.log('[Solus] Daylight fetch error: ' + err.message);
      return;
    }
    var json;
    try { json = JSON.parse(responseText); } catch (e) {
      console.log('[Solus] JSON parse error: ' + e.message);
      return;
    }

    // The API returns ISO 8601 times in UTC.  We convert them to seconds
    // elapsed since local midnight so the watch can compare directly with
    // localtime().
    var midnight = new Date();
    midnight.setHours(0, 0, 0, 0);

    var rise = new Date(json.results.sunrise);
    var set  = new Date(json.results.sunset);

    var riseSeconds = Math.round((rise.getTime() - midnight.getTime()) / 1000);
    var setSeconds  = Math.round((set.getTime()  - midnight.getTime()) / 1000);

    console.log('[Solus] Sunrise: ' + riseSeconds + 's, Sunset: ' + setSeconds + 's');

    var dict = {};
    dict[KEY_SUNRISE] = riseSeconds;
    dict[KEY_SUNSET]  = setSeconds;

    Pebble.sendAppMessage(dict,
      function() { console.log('[Solus] Daylight data sent OK'); },
      function(e) { console.log('[Solus] Daylight send failed: ' + JSON.stringify(e)); }
    );
  });
}

function locationError(err) {
  console.log('[Solus] Location error: ' + err.message);
  // Send default sunrise/sunset so the watch stops requesting every tick.
  // Without this, s_daylight_requested on the watch stays false and it pings
  // the phone on every minute tick indefinitely.
  var dict = {};
  dict[KEY_SUNRISE] = 6  * 3600;
  dict[KEY_SUNSET]  = 18 * 3600;
  Pebble.sendAppMessage(dict,
    function() { console.log('[Solus] Sent default daylight values after location failure'); },
    function() { console.log('[Solus] Failed to send default daylight values'); }
  );
}

function fetchAndSendDaylight() {
  navigator.geolocation.getCurrentPosition(
    requestDaylightData,
    locationError,
    // 5 s is enough for a cached GPS fix; 15 s was blocking the JS thread
    // unnecessarily long on airplane mode or when permission is denied.
    { timeout: 5000, maximumAge: 3600000 }
  );
}

// ─── Pebble event listeners ────────────────────────────────────────────────

// Called once when the JS environment is ready.
Pebble.addEventListener('ready', function() {
  console.log('[Solus] PebbleKit JS ready');
  // Restore the persisted animation-mode so the watch gets it on launch.
  var stored = localStorage.getItem('animMode');
  if (stored !== null) {
    var dict = {};
    dict[KEY_ANIM_MODE] = parseInt(stored, 10);
    Pebble.sendAppMessage(dict,
      function() { console.log('[Solus] Restored animMode: ' + stored); },
      function() { console.log('[Solus] Failed to restore animMode'); }
    );
  }
  // Immediately fetch fresh daylight data.
  fetchAndSendDaylight();
});

// Called when the watch sends us an AppMessage (our "please refresh" ping).
Pebble.addEventListener('appmessage', function(e) {
  console.log('[Solus] Watch requested daylight refresh');
  fetchAndSendDaylight();
});

// Called when the user opens the settings page from the Pebble app.
Pebble.addEventListener('showConfiguration', function() {
  var currentMode = parseInt(localStorage.getItem('animMode') || '0', 10);
  // Inject the current mode value directly into the HTML as a JS literal.
  // We cannot pass it via a data: URI fragment (#hash) because data: URIs are
  // not real URLs — webviews drop or reject the fragment, causing the popup to
  // open and immediately close.
  var html = CONFIG_PAGE_HTML.replace('__CURRENT_MODE__', String(currentMode));
  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
});

// Called when the user closes the settings page.
Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response || e.response === 'CANCELLED') {
    console.log('[Solus] Config closed without saving');
    return;
  }
  var result;
  try { result = JSON.parse(decodeURIComponent(e.response)); } catch (err) {
    console.log('[Solus] Config response parse error: ' + err.message);
    return;
  }
  if (typeof result.animMode !== 'number') {
    console.log('[Solus] Unexpected config payload: ' + JSON.stringify(result));
    return;
  }

  var animMode = result.animMode;
  localStorage.setItem('animMode', animMode);
  console.log('[Solus] Saving animMode: ' + animMode);

  var dict = {};
  dict[KEY_ANIM_MODE] = animMode;
  Pebble.sendAppMessage(dict,
    function() { console.log('[Solus] animMode sent to watch OK'); },
    function(e2) { console.log('[Solus] animMode send failed: ' + JSON.stringify(e2)); }
  );
});
