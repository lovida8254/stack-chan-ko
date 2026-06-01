(function () {
  const slots = document.getElementById('slotsContainer');
  const saveBtn = document.getElementById('saveBtn');
  const saveStatus = document.getElementById('saveStatus');
  const sayNowBtn = document.getElementById('sayNowBtn');
  const sayNowText = document.getElementById('sayNowText');
  const sayNowStatus = document.getElementById('sayNowStatus');

  function setStatus(el, msg, ok) {
    el.textContent = msg;
    el.className = 'status ' + (ok ? 'ok' : 'err');
  }

  function esc(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  // 사운드 드롭다운 후보(SD /sfx 폴더의 mp3). loadSfxFiles() 가 채움.
  let sfxFiles = [];

  function soundOptions(selected) {
    let html = '<option value="">(사운드 없음)</option>';
    sfxFiles.forEach((f) => {
      html += '<option value="' + esc(f) + '"' + (f === selected ? ' selected' : '') + '>' + esc(f) + '</option>';
    });
    // 저장된 값이 현재 목록에 없으면(파일 삭제 등) 그 값도 보존해서 표시.
    if (selected && sfxFiles.indexOf(selected) < 0) {
      html += '<option value="' + esc(selected) + '" selected>' + esc(selected) + ' (없음?)</option>';
    }
    return html;
  }

  function renderSlots(items) {
    slots.innerHTML = '';
    items.forEach((it, idx) => {
      const div = document.createElement('div');
      div.className = 'slot';
      div.innerHTML =
        '<label>슬롯 ' + (idx + 1) + '</label>' +
        '<div class="time-row">' +
          '<input type="number" min="0" max="23" id="hour-' + idx + '" value="' + (it.hour | 0) + '">' +
          ' : ' +
          '<input type="number" min="0" max="59" id="min-' + idx + '" value="' + (it.min | 0) + '">' +
          '<label><input type="checkbox" id="wd-' + idx + '"' + (it.weekdayOnly ? ' checked' : '') + '> 평일만</label>' +
        '</div>' +
        '<label for="sound-' + idx + '">사운드 (멘트보다 먼저 재생)</label>' +
        '<select id="sound-' + idx + '">' + soundOptions(it.sound || '') + '</select>' +
        '<label for="prompt-' + idx + '">멘트 (비우면 사운드만)</label>' +
        '<textarea id="prompt-' + idx + '">' + esc(it.prompt || '') + '</textarea>';
      slots.appendChild(div);
    });
  }

  // 슬롯 렌더 전에 sfx 파일 목록을 먼저 받아온다(드롭다운 채우기용).
  function loadSfxFiles() {
    return fetch('/sfx_get')
      .then(r => r.json())
      .then(data => { sfxFiles = (data.files || []).slice(); })
      .catch(() => { sfxFiles = []; });
  }

  function loadSchedules() {
    // sfx 파일 목록을 먼저 받아 드롭다운을 채운 뒤 슬롯을 렌더한다.
    loadSfxFiles().then(() =>
      fetch('/schedules_get')
        .then(r => r.json())
        .then(data => {
          renderSlots(data.items || []);
          setStatus(saveStatus, '로드 완료 (슬롯 ' + (data.items || []).length + '개)', true);
        })
        .catch(e => setStatus(saveStatus, '로드 실패: ' + e, false))
    );
  }

  saveBtn.addEventListener('click', () => {
    const items = [];
    document.querySelectorAll('.slot').forEach((div, idx) => {
      items.push({
        hour: parseInt(document.getElementById('hour-' + idx).value, 10),
        min: parseInt(document.getElementById('min-' + idx).value, 10),
        weekdayOnly: document.getElementById('wd-' + idx).checked,
        sound: document.getElementById('sound-' + idx).value,
        prompt: document.getElementById('prompt-' + idx).value,
      });
    });
    setStatus(saveStatus, '저장 중...', true);
    fetch('/schedules_set', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ version: 1, items: items }),
    })
      .then(r => r.text().then(t => ({ ok: r.ok, t })))
      .then(o => setStatus(saveStatus, (o.ok ? '저장 완료: ' : '저장 실패: ') + o.t, o.ok))
      .catch(e => setStatus(saveStatus, '저장 실패: ' + e, false));
  });

  sayNowBtn.addEventListener('click', () => {
    const text = sayNowText.value.trim();
    if (!text) {
      setStatus(sayNowStatus, '말할 내용을 입력해주세요.', false);
      return;
    }
    setStatus(sayNowStatus, '전송 중...', true);
    fetch('/speak_now', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain; charset=utf-8' },
      body: text,
    })
      .then(r => r.text().then(t => ({ ok: r.ok, t })))
      .then(o => setStatus(sayNowStatus, (o.ok ? '전송됨: ' : '실패: ') + o.t, o.ok))
      .catch(e => setStatus(sayNowStatus, '실패: ' + e, false));
  });

  // Volume control
  const volSlider = document.getElementById('volSlider');
  const volReadout = document.getElementById('volReadout');
  const volSaveBtn = document.getElementById('volSaveBtn');
  const volStatus = document.getElementById('volStatus');

  function loadVolume() {
    fetch('/volume_get')
      .then(r => r.text())
      .then(t => {
        const v = parseInt(t, 10);
        if (!Number.isFinite(v)) throw new Error('bad response: ' + t);
        volSlider.value = v;
        volReadout.textContent = v + ' (' + Math.round(v * 100 / 255) + '%)';
      })
      .catch(e => setStatus(volStatus, '로드 실패: ' + e, false));
  }

  volSlider.addEventListener('input', () => {
    const v = parseInt(volSlider.value, 10);
    volReadout.textContent = v + ' (' + Math.round(v * 100 / 255) + '%)';
  });

  volSaveBtn.addEventListener('click', () => {
    const v = parseInt(volSlider.value, 10);
    setStatus(volStatus, '저장 중...', true);
    fetch('/volume_set', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain; charset=utf-8' },
      body: String(v),
    })
      .then(r => r.text().then(t => ({ ok: r.ok, t })))
      .then(o => setStatus(volStatus, (o.ok ? '저장됨: ' : '실패: ') + o.t, o.ok))
      .catch(e => setStatus(volStatus, '실패: ' + e, false));
  });

  loadSchedules();
  loadVolume();
})();
