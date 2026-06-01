(function () {
  const $ = (id) => document.getElementById(id);
  function setStatus(el, msg, ok) {
    el.textContent = msg;
    el.className = 'status ' + (ok ? 'ok' : 'err');
  }
  function esc(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }
  function postText(url, text) {
    return fetch(url, { method: 'POST', headers: { 'Content-Type': 'text/plain; charset=utf-8' }, body: text })
      .then(r => r.text().then(t => ({ ok: r.ok, t })));
  }
  function postJson(url, obj) {
    return fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(obj) })
      .then(r => r.text().then(t => ({ ok: r.ok, t })));
  }

  // ---------- 페르소나 / 기억 ----------
  let personas = { active: 0, presets: [] };
  function renderPersona() {
    const sel = $('personaSelect');
    sel.innerHTML = '';
    personas.presets.forEach((p, i) => {
      const o = document.createElement('option');
      o.value = i;
      o.textContent = p.name + (i === personas.active ? ' (사용 중)' : '');
      sel.appendChild(o);
    });
    sel.value = personas.active;
    $('roleInput').value = (personas.presets[personas.active] || {}).role || '';
    const an = (personas.presets[personas.active] || {}).name || '';
    setStatus($('personaActiveLabel'), '현재 페르소나: ' + an, true);
  }
  function loadRole() {
    fetch('/persona_get').then(r => r.json()).then(d => {
      personas = { active: d.active | 0, presets: d.presets || [] };
      renderPersona();
    }).catch(e => setStatus($('roleStatus'), '로드 실패: ' + e, false));
    fetch('/memory_get').then(r => r.text()).then(t => { $('memoryInput').value = t; });
  }
  // Viewing/editing a different preset in the dropdown
  $('personaSelect').addEventListener('change', () => {
    const i = parseInt($('personaSelect').value, 10);
    $('roleInput').value = (personas.presets[i] || {}).role || '';
  });
  // Save edited text into the selected preset (apply live if it is the active one)
  $('roleSaveBtn').addEventListener('click', () => {
    const i = parseInt($('personaSelect').value, 10);
    if (personas.presets[i]) personas.presets[i].role = $('roleInput').value;
    const apply = (i === personas.active);
    setStatus($('roleStatus'), '저장 중...', true);
    postJson('/persona_set', { active: personas.active, presets: personas.presets, apply: apply })
      .then(o => setStatus($('roleStatus'), o.ok ? (apply ? '저장됨 (적용·재연결)' : '저장됨') : '실패: ' + o.t, o.ok))
      .catch(e => setStatus($('roleStatus'), '실패: ' + e, false));
  });
  // Switch the active persona to the selected one (saves edits too, then reconnects)
  $('personaActivateBtn').addEventListener('click', () => {
    const i = parseInt($('personaSelect').value, 10);
    if (personas.presets[i]) personas.presets[i].role = $('roleInput').value;
    personas.active = i;
    setStatus($('roleStatus'), '전환 중... (재연결, 잠시 후 새 성격으로)', true);
    postJson('/persona_set', { active: i, presets: personas.presets, apply: true })
      .then(o => { setStatus($('roleStatus'), o.ok ? '전환됨!' : '실패: ' + o.t, o.ok); if (o.ok) renderPersona(); })
      .catch(e => setStatus($('roleStatus'), '실패: ' + e, false));
  });
  $('memorySaveBtn').addEventListener('click', () => {
    setStatus($('memoryStatus'), '저장 중...', true);
    postText('/memory_set', $('memoryInput').value)
      .then(o => setStatus($('memoryStatus'), o.ok ? '저장됨' : '실패: ' + o.t, o.ok))
      .catch(e => setStatus($('memoryStatus'), '실패: ' + e, false));
  });
  $('memoryClearBtn').addEventListener('click', () => {
    if (!confirm('기억을 비울까요?')) return;
    fetch('/memory_clear').then(r => r.text().then(t => ({ ok: r.ok, t })))
      .then(o => { setStatus($('memoryStatus'), o.ok ? '비움 완료' : '실패: ' + o.t, o.ok); if (o.ok) $('memoryInput').value = ''; })
      .catch(e => setStatus($('memoryStatus'), '실패: ' + e, false));
  });

  // ---------- 지금 발화 ----------
  $('sayNowBtn').addEventListener('click', () => {
    const text = $('sayNowText').value.trim();
    if (!text) { setStatus($('sayNowStatus'), '내용을 입력해주세요.', false); return; }
    setStatus($('sayNowStatus'), '전송 중...', true);
    postText('/speak_now', text)
      .then(o => setStatus($('sayNowStatus'), (o.ok ? '전송됨: ' : '실패: ') + o.t, o.ok))
      .catch(e => setStatus($('sayNowStatus'), '실패: ' + e, false));
  });

  // ---------- 예약 ----------
  let g_schedSfxFiles = [];   // SD /sfx 폴더의 mp3 목록(예약 사운드 드롭다운용)

  function schedSoundOptions(selected) {
    let html = '<option value="">(사운드 없음)</option>';
    g_schedSfxFiles.forEach(f => {
      html += '<option value="' + esc(f) + '"' + (f === selected ? ' selected' : '') + '>' + esc(f) + '</option>';
    });
    if (selected && g_schedSfxFiles.indexOf(selected) < 0) {
      html += '<option value="' + esc(selected) + '" selected>' + esc(selected) + ' (없음?)</option>';
    }
    return html;
  }

  function renderSlots(items) {
    const c = $('slotsContainer'); c.innerHTML = '';
    items.forEach((it, idx) => {
      const div = document.createElement('div');
      div.className = 'slot';
      div.innerHTML =
        '<label>슬롯 ' + (idx + 1) + '</label>' +
        '<div class="row">' +
          '<input type="number" min="0" max="23" id="sh-' + idx + '" value="' + (it.hour | 0) + '"> : ' +
          '<input type="number" min="0" max="59" id="sm-' + idx + '" value="' + (it.min | 0) + '">' +
          '<label><input type="checkbox" id="sw-' + idx + '"' + (it.weekdayOnly ? ' checked' : '') + '> 평일만</label>' +
        '</div>' +
        '<label for="ss-' + idx + '">사운드 (멘트보다 먼저 재생)</label>' +
        '<select id="ss-' + idx + '">' + schedSoundOptions(it.sound || '') + '</select>' +
        '<label for="sp-' + idx + '">멘트 (비우면 사운드만)</label>' +
        '<textarea id="sp-' + idx + '" style="height:3.5em">' + esc(it.prompt || '') + '</textarea>';
      c.appendChild(div);
    });
  }
  function loadSchedules() {
    // sfx 파일 목록을 먼저 받아 드롭다운을 채운 뒤 슬롯을 렌더.
    fetch('/sfx_get').then(r => r.json()).then(d => { g_schedSfxFiles = (d.files || []).slice(); }).catch(() => { g_schedSfxFiles = []; })
      .then(() => fetch('/schedules_get').then(r => r.json()))
      .then(d => renderSlots(d.items || []))
      .catch(e => setStatus($('schedStatus'), '로드 실패: ' + e, false));
  }
  $('schedSaveBtn').addEventListener('click', () => {
    const items = [];
    document.querySelectorAll('#slotsContainer .slot').forEach((div, idx) => {
      items.push({
        hour: parseInt($('sh-' + idx).value, 10),
        min: parseInt($('sm-' + idx).value, 10),
        weekdayOnly: $('sw-' + idx).checked,
        sound: $('ss-' + idx).value,
        prompt: $('sp-' + idx).value,
      });
    });
    setStatus($('schedStatus'), '저장 중...', true);
    postJson('/schedules_set', { version: 1, items })
      .then(o => setStatus($('schedStatus'), (o.ok ? '저장됨: ' : '실패: ') + o.t, o.ok))
      .catch(e => setStatus($('schedStatus'), '실패: ' + e, false));
  });

  // ---------- 볼륨 ----------
  function showVol(v) { $('volReadout').textContent = v + ' (' + Math.round(v * 100 / 255) + '%)'; }
  function loadVolume() {
    fetch('/volume_get').then(r => r.text()).then(t => {
      const v = parseInt(t, 10); if (!Number.isFinite(v)) return;
      $('volSlider').value = v; showVol(v);
    });
  }
  $('volSlider').addEventListener('input', () => showVol(parseInt($('volSlider').value, 10)));
  $('volSaveBtn').addEventListener('click', () => {
    setStatus($('volStatus'), '저장 중...', true);
    postText('/volume_set', String(parseInt($('volSlider').value, 10)))
      .then(o => setStatus($('volStatus'), (o.ok ? '저장됨: ' : '실패: ') + o.t, o.ok))
      .catch(e => setStatus($('volStatus'), '실패: ' + e, false));
  });

  // ---------- 움직임 (Idle) ----------
  function loadIdle() {
    fetch('/idle_get').then(r => r.json()).then(d => {
      $('idleEnabled').checked = !!d.enabled;
      $('idleEnergy').value = d.energy; $('idleEnergyVal').textContent = d.energy;
      $('idleMin').value = d.minIntervalSec; $('idleMax').value = d.maxIntervalSec;
      $('idleQuiet').value = d.quietAfterSec;
      const w = d.exprWeights || [];
      for (let i = 0; i < 6; i++) $('w' + i).value = (w[i] | 0);
      $('idleGesture').checked = !!d.gestureEnabled;
      $('idleGaze').checked = !!d.gazeWander;
      $('idleBlink').checked = !!d.blinkEnabled;
      $('blinkMin').value = d.blinkMinSec; $('blinkMax').value = d.blinkMaxSec;
    }).catch(e => setStatus($('idleStatus'), '로드 실패: ' + e, false));
  }
  $('idleEnergy').addEventListener('input', () => $('idleEnergyVal').textContent = $('idleEnergy').value);
  $('idleSaveBtn').addEventListener('click', () => {
    const obj = {
      enabled: $('idleEnabled').checked,
      energy: parseInt($('idleEnergy').value, 10),
      minIntervalSec: parseInt($('idleMin').value, 10),
      maxIntervalSec: parseInt($('idleMax').value, 10),
      quietAfterSec: parseInt($('idleQuiet').value, 10),
      exprWeights: [0,1,2,3,4,5].map(i => parseInt($('w' + i).value, 10) || 0),
      gestureEnabled: $('idleGesture').checked,
      gazeWander: $('idleGaze').checked,
      blinkEnabled: $('idleBlink').checked,
      blinkMinSec: parseInt($('blinkMin').value, 10),
      blinkMaxSec: parseInt($('blinkMax').value, 10),
    };
    setStatus($('idleStatus'), '저장 중...', true);
    postJson('/idle_set', obj)
      .then(o => setStatus($('idleStatus'), (o.ok ? '저장됨' : '실패: ' + o.t), o.ok))
      .catch(e => setStatus($('idleStatus'), '실패: ' + e, false));
  });

  // ---------- 근접센서 ----------
  let proxPollTimer = null;
  function applyProx(d) {
    $('proxEnabled').checked = !!d.enabled;
    $('proxNear').value = d.nearThreshold;
    $('proxVeryNear').value = d.veryNearThreshold;
    $('proxMaxScale').value = d.maxEyeScale;
    $('proxDetect').innerHTML = d.detected
      ? '센서 감지됨 ✓'
      : '<b style="color:#cc2222">센서 미감지</b> — 재부팅 후에도 안 잡히면 하드웨어 확인 필요';
    $('proxRaw').textContent = d.raw;
    $('proxBar').value = d.raw;
  }
  function loadProx() { fetch('/prox_get').then(r => r.json()).then(applyProx).catch(()=>{}); }
  function pollProxRaw() {
    fetch('/prox_get').then(r => r.json()).then(d => {
      $('proxRaw').textContent = d.raw; $('proxBar').value = d.raw;
      if (!d.detected) $('proxDetect').innerHTML = '<b style="color:#cc2222">센서 미감지</b>';
    }).catch(()=>{});
  }
  // poll live raw value only while the 근접 section is open
  document.querySelectorAll('details').forEach(det => {
    det.addEventListener('toggle', () => {
      const isProx = det.querySelector('#proxBar');
      if (isProx) {
        if (det.open) { pollProxRaw(); proxPollTimer = setInterval(pollProxRaw, 400); }
        else if (proxPollTimer) { clearInterval(proxPollTimer); proxPollTimer = null; }
      }
    });
  });
  $('proxSaveBtn').addEventListener('click', () => {
    const obj = {
      enabled: $('proxEnabled').checked,
      nearThreshold: parseInt($('proxNear').value, 10),
      veryNearThreshold: parseInt($('proxVeryNear').value, 10),
      maxEyeScale: parseFloat($('proxMaxScale').value),
    };
    setStatus($('proxStatus'), '저장 중...', true);
    postJson('/prox_set', obj)
      .then(o => setStatus($('proxStatus'), (o.ok ? '저장됨' : '실패: ' + o.t), o.ok))
      .catch(e => setStatus($('proxStatus'), '실패: ' + e, false));
  });

  // ---------- WiFi ----------
  function netRow(ssid, pwd) {
    const div = document.createElement('div');
    div.className = 'net';
    div.innerHTML =
      '<div class="row"><input type="text" class="wssid" placeholder="SSID" value="' + esc(ssid || '') + '"></div>' +
      '<div class="row" style="margin-top:0.3em"><input type="password" class="wpwd" placeholder="비밀번호" value="' + esc(pwd || '') + '">' +
      '<button type="button" class="ghost wdel">삭제</button></div>';
    div.querySelector('.wdel').addEventListener('click', () => div.remove());
    return div;
  }
  function loadWifi() {
    fetch('/wifi_get').then(r => r.json()).then(d => {
      const st = d.status || {};
      $('wifiStatus').innerHTML = st.connected
        ? ('현재 연결: <b>' + esc(st.ssid) + '</b> · ' + esc(st.ip) + ' · RSSI ' + st.rssi)
        : '현재 연결 안 됨';
      const c = $('wifiNets'); c.innerHTML = '';
      (d.networks || []).forEach(n => c.appendChild(netRow(n.ssid, n.password)));
      if (!(d.networks || []).length) c.appendChild(netRow('', ''));
    }).catch(e => setStatus($('wifiSaveStatus'), '로드 실패: ' + e, false));
  }
  $('wifiAddBtn').addEventListener('click', () => $('wifiNets').appendChild(netRow('', '')));
  $('wifiScanBtn').addEventListener('click', () => {
    $('wifiScanResult').textContent = '검색 중... (몇 초 걸림)';
    fetch('/wifi_scan').then(r => r.json()).then(d => {
      const aps = (d.aps || []).sort((a, b) => b.rssi - a.rssi);
      if (!aps.length) { $('wifiScanResult').textContent = '검색 결과 없음'; return; }
      $('wifiScanResult').innerHTML = '주변: ' + aps.map(a =>
        '<a href="#" data-ssid="' + esc(a.ssid) + '">' + esc(a.ssid) + '</a>(' + a.rssi + ')'
      ).join(' · ');
      $('wifiScanResult').querySelectorAll('a').forEach(a => a.addEventListener('click', ev => {
        ev.preventDefault();
        $('wifiNets').appendChild(netRow(a.getAttribute('data-ssid'), ''));
      }));
    }).catch(e => $('wifiScanResult').textContent = '검색 실패: ' + e);
  });
  function collectWifi() {
    const networks = [];
    document.querySelectorAll('#wifiNets .net').forEach(div => {
      const ssid = div.querySelector('.wssid').value.trim();
      const password = div.querySelector('.wpwd').value;
      if (ssid) networks.push({ ssid, password });
    });
    return { version: 1, networks };
  }
  function saveWifi() {
    return postJson('/wifi_set', collectWifi());
  }
  $('wifiSaveBtn').addEventListener('click', () => {
    setStatus($('wifiSaveStatus'), '저장 중...', true);
    saveWifi().then(o => setStatus($('wifiSaveStatus'), (o.ok ? '저장됨 (재부팅 후 적용)' : '실패: ' + o.t), o.ok))
      .catch(e => setStatus($('wifiSaveStatus'), '실패: ' + e, false));
  });
  $('rebootBtn').addEventListener('click', () => {
    if (!confirm('WiFi 설정을 저장하고 재부팅할까요?')) return;
    setStatus($('wifiSaveStatus'), '저장 중...', true);
    saveWifi().then(o => {
      if (!o.ok) { setStatus($('wifiSaveStatus'), '저장 실패: ' + o.t, false); return; }
      setStatus($('wifiSaveStatus'), '재부팅 중... 잠시 후 새 설정으로 연결됩니다.', true);
      fetch('/reboot', { method: 'POST' }).catch(() => {});
    }).catch(e => setStatus($('wifiSaveStatus'), '실패: ' + e, false));
  });

  // ---------- 능동 발화 ----------
  const linesToArr = (s) => s.split('\n').map(x => x.trim()).filter(Boolean);
  const arrToLines = (a) => (a || []).join('\n');
  function loadTalk() {
    fetch('/talk_get').then(r => r.json()).then(d => {
      $('talkEnabled').checked = !!d.enabled;
      $('talkMin').value = d.minQuietSec; $('talkMax').value = d.maxQuietSec;
      $('talkPrompts').value = arrToLines(d.prompts);
      $('talkApproach').checked = !!d.approachEnabled;
      $('talkApproachQuiet').value = d.approachQuietSec;
      $('talkApproachPrompts').value = arrToLines(d.approachPrompts);
    }).catch(e => setStatus($('talkStatus'), '로드 실패: ' + e, false));
  }
  $('talkSaveBtn').addEventListener('click', () => {
    const obj = {
      enabled: $('talkEnabled').checked,
      minQuietSec: parseInt($('talkMin').value, 10),
      maxQuietSec: parseInt($('talkMax').value, 10),
      prompts: linesToArr($('talkPrompts').value),
      approachEnabled: $('talkApproach').checked,
      approachQuietSec: parseInt($('talkApproachQuiet').value, 10),
      approachPrompts: linesToArr($('talkApproachPrompts').value),
    };
    setStatus($('talkStatus'), '저장 중...', true);
    postJson('/talk_set', obj)
      .then(o => setStatus($('talkStatus'), (o.ok ? '저장됨' : '실패: ' + o.t), o.ok))
      .catch(e => setStatus($('talkStatus'), '실패: ' + e, false));
  });

  // ---------- 쓰담 반응 ----------
  function loadPet() {
    fetch('/pet_get').then(r => r.json()).then(d => {
      $('petEnabled').checked = !!d.enabled;
      $('petSens').value = d.sensitivity; $('petSensVal').textContent = d.sensitivity;
      $('petSpeak').checked = !!d.speakOnPet;
      $('petPrompts').value = arrToLines(d.prompts);
      $('petDetect').innerHTML = d.detected ? 'IMU 감지됨 ✓' : '<b style="color:#cc2222">IMU 미감지</b>';
    }).catch(e => setStatus($('petStatus'), '로드 실패: ' + e, false));
  }
  $('petSens').addEventListener('input', () => $('petSensVal').textContent = $('petSens').value);
  $('petSaveBtn').addEventListener('click', () => {
    const obj = {
      enabled: $('petEnabled').checked,
      sensitivity: parseInt($('petSens').value, 10),
      speakOnPet: $('petSpeak').checked,
      prompts: linesToArr($('petPrompts').value),
    };
    setStatus($('petStatus'), '저장 중...', true);
    postJson('/pet_set', obj)
      .then(o => setStatus($('petStatus'), (o.ok ? '저장됨' : '실패: ' + o.t), o.ok))
      .catch(e => setStatus($('petStatus'), '실패: ' + e, false));
  });

  // ---------- 터치 반응 ----------
  function loadTouch() {
    fetch('/touch_get').then(r => r.json()).then(d => {
      $('touchLook').checked = !!d.lookEnabled;
      $('touchStroke').checked = !!d.strokeEnabled;
      $('touchThresh').value = d.strokeThreshold;
    }).catch(e => setStatus($('touchStatus'), '로드 실패: ' + e, false));
  }
  $('touchSaveBtn').addEventListener('click', () => {
    const obj = {
      lookEnabled: $('touchLook').checked,
      strokeEnabled: $('touchStroke').checked,
      strokeThreshold: parseInt($('touchThresh').value, 10),
    };
    setStatus($('touchStatus'), '저장 중...', true);
    postJson('/touch_set', obj)
      .then(o => setStatus($('touchStatus'), (o.ok ? '저장됨' : '실패: ' + o.t), o.ok))
      .catch(e => setStatus($('touchStatus'), '실패: ' + e, false));
  });

  // ---------- 배터리 반응 ----------
  function loadBatt() {
    fetch('/batt_get').then(r => r.json()).then(d => {
      $('battEnabled').checked = !!d.enabled;
      $('battLowReact').checked = !!d.lowReact;
      $('battLowThresh').value = d.lowThreshold;
      $('battLowInterval').value = d.lowIntervalMin;
      $('battLowPrompt').value = d.lowPrompt || '';
      $('battChargeReact').checked = !!d.chargeReact;
      $('battChargePrompt').value = d.chargePrompt || '';
      $('battLive').innerHTML = '현재: ' + d.level + '% ' + (d.charging ? '⚡충전 중' : '(방전 중)');
    }).catch(e => setStatus($('battStatus'), '로드 실패: ' + e, false));
  }
  $('battSaveBtn').addEventListener('click', () => {
    const obj = {
      enabled: $('battEnabled').checked,
      lowReact: $('battLowReact').checked,
      lowThreshold: parseInt($('battLowThresh').value, 10),
      lowIntervalMin: parseInt($('battLowInterval').value, 10),
      lowPrompt: $('battLowPrompt').value,
      chargeReact: $('battChargeReact').checked,
      chargePrompt: $('battChargePrompt').value,
    };
    setStatus($('battStatus'), '저장 중...', true);
    postJson('/batt_set', obj)
      .then(o => setStatus($('battStatus'), (o.ok ? '저장됨' : '실패: ' + o.t), o.ok))
      .catch(e => setStatus($('battStatus'), '실패: ' + e, false));
  });

  // ---------- 밤 모드 ----------
  function loadNight() {
    fetch('/night_get').then(r => r.json()).then(d => {
      $('nightEnabled').checked = !!d.enabled;
      $('nightStart').value = d.startHour; $('nightEnd').value = d.endHour;
      $('nightDayB').value = d.dayBrightness; $('nightNightB').value = d.nightBrightness;
      $('nightSleepy').checked = !!d.sleepyBias;
      $('nightGreet').checked = !!d.greetEnabled;
      $('nightGreetPrompt').value = d.greetPrompt || '';
    }).catch(e => setStatus($('nightStatus'), '로드 실패: ' + e, false));
  }
  $('nightSaveBtn').addEventListener('click', () => {
    const obj = {
      enabled: $('nightEnabled').checked,
      startHour: parseInt($('nightStart').value, 10),
      endHour: parseInt($('nightEnd').value, 10),
      dayBrightness: parseInt($('nightDayB').value, 10),
      nightBrightness: parseInt($('nightNightB').value, 10),
      sleepyBias: $('nightSleepy').checked,
      greetEnabled: $('nightGreet').checked,
      greetPrompt: $('nightGreetPrompt').value,
    };
    setStatus($('nightStatus'), '저장 중...', true);
    postJson('/night_set', obj)
      .then(o => setStatus($('nightStatus'), (o.ok ? '저장됨' : '실패: ' + o.t), o.ok))
      .catch(e => setStatus($('nightStatus'), '실패: ' + e, false));
  });

  // ---------- 눈 색 (감정별 그라데이션) ----------
  const EYE_EMOTIONS = [['neutral', '중립'], ['happy', '기쁨'], ['sleepy', '졸림'], ['doubt', '갸웃'], ['sad', '슬픔'], ['angry', '화남']];
  function eyeColorRender(data) {
    const c = $('eyeColorList'); c.innerHTML = '';
    EYE_EMOTIONS.forEach(([key, label]) => {
      const d = data[key] || { top: '#FFFFFF', bot: '#FF8800' };
      const div = document.createElement('div');
      div.className = 'row';
      div.style.cssText = 'gap:0.6em; margin-bottom:0.4em';
      div.innerHTML =
        '<span style="min-width:3em; font-weight:bold">' + label + '</span>' +
        '<label style="font-weight:normal; margin:0">위 <input type="color" class="ec-top" data-k="' + key + '" value="' + esc(d.top) + '"></label>' +
        '<label style="font-weight:normal; margin:0">아래 <input type="color" class="ec-bot" data-k="' + key + '" value="' + esc(d.bot) + '"></label>';
      c.appendChild(div);
    });
  }
  function loadEyeColor() {
    fetch('/eyecolor_get').then(r => r.json())
      .then(d => eyeColorRender(d))
      .catch(e => setStatus($('eyeColorStatus'), '로드 실패: ' + e, false));
  }
  $('eyeColorSaveBtn').addEventListener('click', () => {
    const obj = {};
    EYE_EMOTIONS.forEach(([key]) => {
      const top = document.querySelector('.ec-top[data-k="' + key + '"]').value;
      const bot = document.querySelector('.ec-bot[data-k="' + key + '"]').value;
      obj[key] = { top: top, bot: bot };
    });
    setStatus($('eyeColorStatus'), '저장 중...', true);
    postJson('/eyecolor_set', obj)
      .then(o => setStatus($('eyeColorStatus'), o.ok ? '저장됨 (즉시 적용)' : '실패: ' + o.t, o.ok))
      .catch(e => setStatus($('eyeColorStatus'), '실패: ' + e, false));
  });

  // ---------- 효과음 (SFX) ----------
  const SFX_EVENTS = [['', '(없음·음성전용)'], ['pet', '쓰담'], ['approach', '다가옴'], ['boot', '부팅'], ['alarm', '알람'],
    ['happy', '기쁨'], ['sad', '슬픔'], ['angry', '화남'], ['doubt', '의심'], ['sleepy', '졸림'], ['neutral', '중립'],
    ['sleep', '취침'], ['wake', '기상'], ['surprise', '깜짝'], ['charge', '충전'], ['low', '배터리부족']];
  let g_motionNames = [];   // /motions_get 에서 채움(사운드에 연동할 모션 선택지)
  let g_sfxFiles = [];      // SD /sfx 폴더의 mp3 목록(파일 드롭다운용, /sfx_get 의 files)
  function motionOpts(sel) {
    return '<option value="">(모션 없음)</option>' +
      g_motionNames.map(m => '<option value="' + esc(m) + '"' + (m === (sel || '') ? ' selected' : '') + '>' + esc(m) + '</option>').join('');
  }
  function fileOpts(sel) {
    let html = '<option value="">(파일 선택)</option>' +
      g_sfxFiles.map(f => '<option value="' + esc(f) + '"' + (f === (sel || '') ? ' selected' : '') + '>' + esc(f) + '</option>').join('');
    // 저장된 파일이 현재 SD 목록에 없으면(삭제 등) 그 값도 보존해서 표시.
    if (sel && g_sfxFiles.indexOf(sel) < 0) html += '<option value="' + esc(sel) + '" selected>' + esc(sel) + ' (없음?)</option>';
    return html;
  }
  function sfxAddRow(name, file, event, motion) {
    const div = document.createElement('div');
    div.className = 'row';
    div.style.cssText = 'gap:0.3em; margin-bottom:0.3em; flex-wrap:wrap';
    const opts = SFX_EVENTS.map(e => '<option value="' + e[0] + '"' + (e[0] === (event || '') ? ' selected' : '') + '>' + e[1] + '</option>').join('');
    div.innerHTML =
      '<input type="text" class="sName" placeholder="이름(예: 생일축하)" style="flex:1;min-width:90px" value="' + esc(name || '') + '">' +
      '<select class="sFile" style="flex:1;min-width:90px">' + fileOpts(file) + '</select>' +
      '<select class="sEvent">' + opts + '</select>' +
      '<select class="sMotion" title="함께 재생할 머리 모션">' + motionOpts(motion) + '</select>' +
      '<button type="button" class="sTest secondary">▶</button>' +
      '<button type="button" class="sDel ghost">✕</button>';
    div.querySelector('.sTest').addEventListener('click', () => {
      const f = div.querySelector('.sFile').value.trim();
      setStatus($('sfxStatus'), '재생 시도: ' + f, true);
      postText('/sfx_test', f).then(o => setStatus($('sfxStatus'), (o.ok ? '결과: ' : '실패: ') + o.t, o.ok)).catch(e => setStatus($('sfxStatus'), '실패: ' + e, false));
    });
    div.querySelector('.sDel').addEventListener('click', () => div.remove());
    $('sfxList').appendChild(div);
  }
  function loadSfx() {
    Promise.all([
      fetch('/motions_get').then(r => r.json()).catch(() => ({ motions: [] })),
      fetch('/sfx_get').then(r => r.json()),
    ]).then(([md, d]) => {
      g_motionNames = (md.motions || []).map(m => m.name);
      g_sfxFiles = (d.files || []).slice();   // 파일 드롭다운 후보(행 추가 전에 채움)
      $('sfxEnabled').checked = !!d.enabled;
      $('sfxList').innerHTML = '';
      (d.sounds || []).forEach(s => sfxAddRow(s.name, s.file, s.event, s.motion));
      $('sfxFiles').textContent = 'SD에 있는 파일: ' + ((d.files && d.files.length) ? d.files.join(', ') : '(없음 — SD sfx 폴더에 MP3를 넣으세요)');
    }).catch(e => setStatus($('sfxStatus'), '로드 실패: ' + e, false));
  }
  $('sfxAddBtn').addEventListener('click', () => sfxAddRow('', '', '', ''));
  $('sfxSaveBtn').addEventListener('click', () => {
    const sounds = [];
    document.querySelectorAll('#sfxList .row').forEach(r => {
      const name = r.querySelector('.sName').value.trim();
      const file = r.querySelector('.sFile').value.trim();
      const event = r.querySelector('.sEvent').value;
      const motion = r.querySelector('.sMotion').value;
      if (name || file) sounds.push({ name: name, file: file, event: event, motion: motion });
    });
    setStatus($('sfxStatus'), '저장 중...', true);
    postJson('/sfx_set', { enabled: $('sfxEnabled').checked, sounds: sounds })
      .then(o => setStatus($('sfxStatus'), o.ok ? '저장됨' : '실패: ' + o.t, o.ok))
      .catch(e => setStatus($('sfxStatus'), '실패: ' + e, false));
  });

  // ---------- 포토프레임 ----------
  function loadPhoto() {
    fetch('/photo_get').then(r => r.json()).then(d => {
      $('photoAuto').checked = (d.autoSlide !== false);
      $('photoSlide').value = d.slideSec || 60;
      const sel = $('photoFolder'); sel.innerHTML = '';
      const o0 = document.createElement('option'); o0.value = ''; o0.textContent = '(기본 photo 폴더)'; sel.appendChild(o0);
      (d.folders || []).forEach(f => { const o = document.createElement('option'); o.value = f; o.textContent = f; sel.appendChild(o); });
      sel.value = d.folder || '';
      $('photoFolderNote').textContent = (d.folders && d.folders.length) ? ('하위 앨범: ' + d.folders.join(', ')) : '하위 폴더 없음 (photo/ 루트 사용)';
    }).catch(e => setStatus($('photoStatus'), '로드 실패: ' + e, false));
  }
  $('photoSaveBtn').addEventListener('click', () => {
    setStatus($('photoStatus'), '저장 중...', true);
    postJson('/photo_set', { autoSlide: $('photoAuto').checked, slideSec: parseInt($('photoSlide').value, 10), folder: $('photoFolder').value })
      .then(o => setStatus($('photoStatus'), o.ok ? '저장됨 (다음 진입 시 적용)' : '실패: ' + o.t, o.ok))
      .catch(e => setStatus($('photoStatus'), '실패: ' + e, false));
  });

  // ---------- init ----------
  // ---------- 머리 조작 (조이스틱) ----------
  (function initHeadPad() {
    const cv = $('headPad'); if (!cv) return;
    const ctx = cv.getContext('2d');
    const W = cv.width, H = cv.height, cx = W / 2, cy = H / 2, R = W / 2 - 18;
    let px = cx, py = cy, dragging = false, lastSent = 0, curNx = 0, curNy = 0, kaTimer = null;
    function draw() {
      ctx.clearRect(0, 0, W, H);
      ctx.strokeStyle = '#333'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(cx, 10); ctx.lineTo(cx, H - 10); ctx.moveTo(10, cy); ctx.lineTo(W - 10, cy); ctx.stroke();
      ctx.beginPath(); ctx.arc(cx, cy, R, 0, 7); ctx.stroke();
      ctx.fillStyle = dragging ? '#39d' : '#7ad';
      ctx.beginPath(); ctx.arc(px, py, 16, 0, 7); ctx.fill();
    }
    function sendXY(nx, ny, force) {
      const st = $('headPadStatus');
      if (st) st.textContent = 'v4 x:' + nx.toFixed(2) + ' y:' + ny.toFixed(2);
      const now = Date.now();
      if (!force && now - lastSent < 40) return;   // ~25Hz 스로틀
      lastSent = now;
      fetch('/servo_move', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: nx.toFixed(2) + ',' + ny.toFixed(2) }).catch(() => {});
    }
    function moveTo(clientX, clientY) {
      const r = cv.getBoundingClientRect();
      let dx = clientX - r.left - cx, dy = clientY - r.top - cy;
      const d = Math.hypot(dx, dy);
      if (d > R) { dx = dx * R / d; dy = dy * R / d; }
      px = cx + dx; py = cy + dy;
      curNx = dx / R; curNy = dy / R;   // 위로 드래그(dy<0)→ -y → 머리 위 (실측: +y=고개 아래)
      draw(); sendXY(curNx, curNy, false);
    }
    function release() { dragging = false; draw(); }   // 놓아도 그 위치 유지(keepalive가 hold) — 중앙 복귀 안 함
    cv.addEventListener('pointerdown', e => {
      dragging = true; cv.setPointerCapture(e.pointerId);
      if (det && det.open && !kaTimer) kaTimer = setInterval(() => sendXY(curNx, curNy, true), 800);   // 드래그 재개 시 keepalive 복구
      moveTo(e.clientX, e.clientY);
    });
    cv.addEventListener('pointermove', e => { if (dragging) moveTo(e.clientX, e.clientY); });
    cv.addEventListener('pointerup', release);
    cv.addEventListener('pointercancel', release);
    // 조작 패널이 열려있는 동안 수동 모드 유지(keepalive) → 그동안 머리가 스스로 안 움직임.
    // 닫으면 keepalive 중단 → 2초 뒤 평소 동작 복귀.
    const det = cv.closest('details');
    if (det) det.addEventListener('toggle', () => {
      if (det.open) { if (!kaTimer) kaTimer = setInterval(() => sendXY(curNx, curNy, true), 800); }
      else if (kaTimer) { clearInterval(kaTimer); kaTimer = null; }
    });
    // 홈 보정 버튼 — 저장 시 오프셋이 트림에 흡수되므로 노브를 중앙으로 리셋.
    const hSt = $('homeStatus');
    const sBtn = $('homeSaveBtn'), rBtn = $('homeResetBtn');
    function recenterKnob() { px = cx; py = cy; curNx = 0; curNy = 0; draw(); }
    if (sBtn) sBtn.addEventListener('click', () => {
      fetch('/servo_home_save', { method: 'POST' }).then(r => r.text())
        .then(t => { if (hSt) hSt.textContent = '홈 저장됨 (' + t + ')'; recenterKnob(); })
        .catch(e => { if (hSt) hSt.textContent = '실패: ' + e; });
    });
    if (rBtn) rBtn.addEventListener('click', () => {
      fetch('/servo_home_reset', { method: 'POST' }).then(r => r.text())
        .then(() => { if (hSt) hSt.textContent = '홈 초기화됨'; recenterKnob(); })
        .catch(e => { if (hSt) hSt.textContent = '실패: ' + e; });
    });

    // ── 움직임 녹화 / 재생 / 목록 ──
    const recBtn = $('recBtn'), recName = $('recName'), recSt = $('recStatus'), motList = $('motionList');
    let recording = false, recSteps = [], recTimer = null;
    function loadMotions() {
      if (!motList) return;
      fetch('/motions_get').then(r => r.json()).then(d => {
        motList.innerHTML = '';
        (d.motions || []).forEach(m => {
          const row = document.createElement('div'); row.className = 'net';
          row.appendChild(document.createTextNode(m.name + ' (' + m.steps + ') '));
          const pb = document.createElement('button'); pb.type = 'button'; pb.textContent = '▶ 재생'; pb.className = 'secondary';
          pb.onclick = () => {
            if (kaTimer) { clearInterval(kaTimer); kaTimer = null; }   // 재생 중 수동 keepalive 중단(안 그럼 재생을 막음)
            fetch('/motion_play', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: m.name });
          };
          const db = document.createElement('button'); db.type = 'button'; db.textContent = '✕'; db.className = 'secondary';
          db.onclick = () => fetch('/motion_delete', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: m.name }).then(loadMotions);
          row.appendChild(pb); row.appendChild(db); motList.appendChild(row);
        });
        if (!(d.motions || []).length) motList.textContent = '(저장된 움직임 없음)';
      }).catch(() => {});
    }
    function stopRec() {
      recording = false; if (recTimer) { clearInterval(recTimer); recTimer = null; }
      if (recBtn) recBtn.textContent = '● 녹화 시작';
      if (recSteps.length < 3) { if (recSt) recSt.textContent = '녹화된 동작이 없어요'; return; }
      const nm = ((recName && recName.value) || '').trim() || ('모션' + (Date.now() % 1000));
      if (recSt) recSt.textContent = '저장 중...';
      const body = JSON.stringify({ name: nm, steps: recSteps });
      recSteps = [];
      fetch('/motion_save', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body })
        .then(r => r.text().then(t => ({ ok: r.ok, t })))
        .then(o => { if (recSt) recSt.textContent = o.ok ? ('저장됨: ' + nm) : ('실패: ' + o.t); loadMotions(); })
        .catch(e => { if (recSt) recSt.textContent = '실패: ' + e; });
    }
    if (recBtn) recBtn.addEventListener('click', () => {
      if (!recording) {
        recording = true; recSteps = []; recBtn.textContent = '■ 정지 및 저장';
        if (recSt) recSt.textContent = '녹화 중... 조이스틱으로 머리를 움직여요';
        recTimer = setInterval(() => {
          if (recSteps.length >= 120 * 3) { stopRec(); return; }   // 최대 ~9.6초
          recSteps.push(Math.round(curNx * 100), Math.round(curNy * 100), 80);
        }, 80);
      } else stopRec();
    });
    loadMotions();

    draw();
  })();

  loadRole();
  loadSfx();
  loadPhoto();
  loadSchedules();
  loadVolume();
  loadIdle();
  loadProx();
  loadTalk();
  loadPet();
  loadTouch();
  loadBatt();
  loadNight();
  loadEyeColor();
  loadWifi();
})();
