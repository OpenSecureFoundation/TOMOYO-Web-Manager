(function () {
  'use strict';

  /* ---------------- generic helpers: toast / modal ---------------- */

  function toast(message, kind) {
    const box = document.getElementById('toast');
    const el = document.createElement('div');
    el.className = 'toast-item ' + (kind === 'err' ? 'err' : 'ok');
    el.textContent = message;
    box.appendChild(el);
    setTimeout(() => el.remove(), 4200);
  }

  let modalConfirmHandler = null;
  const modalBackdrop = document.getElementById('modalBackdrop');
  document.getElementById('modalCancel').addEventListener('click', hideModal);
  document.getElementById('modalConfirm').addEventListener('click', () => {
    const h = modalConfirmHandler;
    hideModal();
    if (h) h();
  });
  modalBackdrop.addEventListener('click', (e) => { if (e.target === modalBackdrop) hideModal(); });

  function showModal(title, bodyHtml, onConfirm) {
    document.getElementById('modalTitle').textContent = title;
    document.getElementById('modalBody').innerHTML = bodyHtml;
    modalConfirmHandler = onConfirm;
    modalBackdrop.classList.add('show');
  }
  function hideModal() {
    modalBackdrop.classList.remove('show');
    modalConfirmHandler = null;
  }

  /* ---------------- mode tag rendering ---------------- */

  function modeTag(mode) {
    if (mode === 'learning')  return '<span class="tag tag-learn">[LEARNING]</span>';
    if (mode === 'enforcing') return '<span class="tag tag-allow">[ENFORCING]</span>';
    return '<span class="tag tag-neutral">[DISABLED]</span>';
  }

  /* ---------------- navigation ---------------- */

  const views = ['domains', 'editor', 'logs', 'history'];
  function showView(name) {
    views.forEach((v) => {
      document.getElementById('view-' + v).classList.toggle('active', v === name);
    });
    document.querySelectorAll('.nav-item').forEach((btn) => {
      btn.classList.toggle('active', btn.dataset.view === name);
    });
    if (name === 'domains') loadDomains();
    if (name === 'editor') loadEditor();
    if (name === 'logs') loadLogs();
    if (name === 'history') loadHistory();
  }
  document.querySelectorAll('.nav-item').forEach((btn) => {
    btn.addEventListener('click', () => showView(btn.dataset.view));
  });

  /* ---------------- session bootstrap ---------------- */

  async function bootstrap() {
    let session;
    try {
      session = await TWM.api('/api/session');
    } catch (e) {
      window.location.href = 'index.html';
      return;
    }
    if (!session.authenticated) {
      window.location.href = 'index.html';
      return;
    }
    document.getElementById('userLabel').textContent = session.user;

    const ind = document.getElementById('kernelIndicator');
    const indText = document.getElementById('kernelIndicatorText');
    if (session.real_kernel_mode) {
      ind.className = 'kernel-indicator real';
      indText.textContent = 'noyau reel';
    } else {
      ind.className = 'kernel-indicator mock';
      indText.textContent = 'mode demo (mock)';
    }

    loadDomains();
  }

  document.getElementById('logoutBtn').addEventListener('click', async () => {
    try { await TWM.api('/api/logout', { method: 'POST' }); } catch (e) { /* ignore */ }
    window.location.href = 'index.html';
  });

  /* ==================================================================
     VIEW: DOMAINS / POLICIES
     ================================================================== */

  let domainsCache = [];

  async function loadDomains() {
    const panel = document.getElementById('domainsPanel');
    panel.innerHTML = '<div class="empty-state">Chargement...</div>';
    try {
      const data = await TWM.api('/api/domains');
      domainsCache = data.domains || [];
      renderDomains(data);
      populateEditorDomainSelect();
    } catch (e) {
      panel.innerHTML = '<div class="empty-state">Erreur : ' + TWM.escapeHtml(e.message) + '</div>';
    }
  }

  function renderDomains(data) {
    const panel = document.getElementById('domainsPanel');
    if (!data.domains || data.domains.length === 0) {
      panel.innerHTML = '<div class="empty-state">Aucun domaine actif trouve.'
        + '<span class="mono">domain_policy est vide ou TOMOYO vient d\'etre initialise</span></div>';
      return;
    }
    panel.innerHTML = data.domains.map((d, i) => `
      <div class="domain-row">
        <div>
          <div class="domain-name">${TWM.escapeHtml(d.name)}</div>
          <div class="domain-meta">profil ${d.profile}</div>
        </div>
        ${modeTag(d.mode)}
        <div class="btn-group">
          <button class="btn btn-sm" data-editrules="${i}">Regles</button>
        </div>
        <div class="btn-group">
          <button class="btn btn-sm" data-mode="learning" data-idx="${i}" ${d.mode === 'learning' ? 'disabled' : ''}>Learning</button>
          <button class="btn btn-sm" data-mode="enforcing" data-idx="${i}" ${d.mode === 'enforcing' ? 'disabled' : ''}>Enforcing</button>
        </div>
      </div>`).join('');

    panel.querySelectorAll('[data-editrules]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const d = domainsCache[Number(btn.dataset.editrules)];
        showView('editor');
        setTimeout(() => selectEditorDomain(d.name), 0);
      });
    });

    panel.querySelectorAll('[data-mode]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const d = domainsCache[Number(btn.dataset.idx)];
        const targetMode = btn.dataset.mode;
        confirmModeSwitch(d, targetMode);
      });
    });
  }

  function confirmModeSwitch(domain, targetMode) {
    const label = targetMode === 'learning' ? 'Learning' : 'Enforcement';
    showModal(
      'Basculer le mode',
      `Basculer <span class="mono">${TWM.escapeHtml(domain.name)}</span> vers <b>${label}</b> ?`
        + '<br><br>Cette action modifie immediatement le comportement du confinement pour ce domaine.',
      async () => {
        try {
          await TWM.api('/api/mode', { method: 'POST', body: { domain: domain.name, mode: targetMode } });
          toast('Mode mis a jour : ' + label);
          loadDomains();
        } catch (e) {
          toast(e.message || 'Echec de la bascule de mode.', 'err');
        }
      }
    );
  }

  document.getElementById('refreshDomainsBtn').addEventListener('click', loadDomains);

  /* ==================================================================
     VIEW: EDITOR
     ================================================================== */

  let currentEditorDomain = null;

  function populateEditorDomainSelect() {
    const sel = document.getElementById('editorDomainSelect');
    const prev = sel.value;
    sel.innerHTML = domainsCache.map((d) => `<option value="${TWM.escapeHtml(d.name)}">${TWM.escapeHtml(d.name)}</option>`).join('');
    if (prev && domainsCache.some((d) => d.name === prev)) {
      sel.value = prev;
    } else if (currentEditorDomain && domainsCache.some((d) => d.name === currentEditorDomain)) {
      sel.value = currentEditorDomain;
    }
  }

  async function loadEditor() {
    if (domainsCache.length === 0) await loadDomains();
    populateEditorDomainSelect();
    const sel = document.getElementById('editorDomainSelect');
    if (sel.value) {
      currentEditorDomain = sel.value;
      loadRules(currentEditorDomain);
    }
  }

  function selectEditorDomain(name) {
    const sel = document.getElementById('editorDomainSelect');
    sel.value = name;
    currentEditorDomain = name;
    loadRules(name);
  }

  document.getElementById('editorDomainSelect').addEventListener('change', (e) => {
    currentEditorDomain = e.target.value;
    loadRules(currentEditorDomain);
  });

  async function loadRules(domain) {
    const list = document.getElementById('rulesList');
    list.innerHTML = '<div class="empty-state">Chargement...</div>';
    try {
      const data = await TWM.api('/api/rules?domain=' + encodeURIComponent(domain));
      renderRules(data.rules || []);
    } catch (e) {
      list.innerHTML = '<div class="empty-state">Erreur : ' + TWM.escapeHtml(e.message) + '</div>';
    }
  }

  function renderRules(rules) {
    const list = document.getElementById('rulesList');
    if (rules.length === 0) {
      list.innerHTML = '<div class="empty-state">Aucune regle pour ce domaine.</div>';
      return;
    }
    list.innerHTML = rules.map((r) => `
      <div class="rule-line">
        <span class="rule-text">${TWM.escapeHtml(r)}</span>
        <button class="delete-x" data-rule="${TWM.escapeHtml(r)}" title="Supprimer">&times;</button>
      </div>`).join('');

    list.querySelectorAll('[data-rule]').forEach((btn) => {
      btn.addEventListener('click', () => deleteRule(btn.dataset.rule));
    });
  }

  async function deleteRule(rule) {
    if (!currentEditorDomain) return;
    try {
      await TWM.api('/api/rules', {
        method: 'POST',
        body: { domain: currentEditorDomain, action: 'delete', rule },
      });
      toast('Regle supprimee.');
      loadRules(currentEditorDomain);
    } catch (e) {
      toast(e.message || 'Echec de la suppression.', 'err');
    }
  }

  document.getElementById('addRuleBtn').addEventListener('click', async () => {
    const input = document.getElementById('newRuleInput');
    const errBox = document.getElementById('ruleError');
    errBox.classList.remove('show');

    const rule = input.value.trim();
    if (!currentEditorDomain) { toast('Selectionnez un domaine.', 'err'); return; }
    if (!rule) { toast('La regle est vide.', 'err'); return; }

    try {
      await TWM.api('/api/rules', {
        method: 'POST',
        body: { domain: currentEditorDomain, action: 'add', rule },
      });
      input.value = '';
      toast('Regle appliquee.');
      loadRules(currentEditorDomain);
    } catch (e) {
      errBox.textContent = e.message || 'Erreur de syntaxe.';
      errBox.classList.add('show');
    }
  });

  document.getElementById('rollbackBtn').addEventListener('click', () => {
    showModal(
      'Retour arriere',
      'Restaurer la derniere sauvegarde de <span class="mono">domain_policy</span> ?'
        + '<br><br>La modification la plus recente (ajout ou suppression de regle) sera annulee.',
      async () => {
        try {
          await TWM.api('/api/rollback', { method: 'POST' });
          toast('Retour arriere effectue.');
          if (currentEditorDomain) loadRules(currentEditorDomain);
        } catch (e) {
          toast(e.message || 'Rollback impossible.', 'err');
        }
      }
    );
  });

  document.getElementById('genCaptureBtn').addEventListener('click', async () => {
    const input = document.getElementById('genExecPath');
    const execPath = input.value.trim();
    if (!execPath) { toast('Indiquez un chemin executable.', 'err'); return; }
    try {
      const res = await TWM.api('/api/generate', { method: 'POST', body: { exec_path: execPath } });
      toast('Capture demarree pour ' + res.domain + ' (pid ' + res.pid + ').');
      loadDomains();
    } catch (e) {
      toast(e.message || 'Echec de la capture.', 'err');
    }
  });

  document.getElementById('exportBtn').addEventListener('click', async () => {
    if (!currentEditorDomain) { toast('Selectionnez un domaine.', 'err'); return; }
    try {
      const res = await fetch('/api/export?domain=' + encodeURIComponent(currentEditorDomain), { credentials: 'same-origin' });
      if (!res.ok) throw new Error('Export impossible (HTTP ' + res.status + ')');
      const text = await res.text();
      const blob = new Blob([text], { type: 'text/plain' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      const safeName = currentEditorDomain.replace(/[^a-z0-9]+/gi, '_').slice(0, 60) || 'domain';
      a.href = url;
      a.download = safeName + '.policy';
      document.body.appendChild(a);
      a.click();
      a.remove();
      URL.revokeObjectURL(url);
    } catch (e) {
      toast(e.message || 'Export impossible.', 'err');
    }
  });

  /* ==================================================================
     VIEW: LOGS
     ================================================================== */

  function parseLogLine(line) {
    // Expected shape: "<ISO-timestamp> granted|denied domain=\"...\" <rest>"
    const m = line.match(/^(\S+)\s+(granted|denied)\s+(.*)$/i);
    if (!m) return { ts: '', result: 'unknown', detail: line };
    return { ts: m[1], result: m[2].toLowerCase(), detail: m[3] };
  }

  async function loadLogs() {
    const panel = document.getElementById('logsPanel');
    panel.innerHTML = '<div class="empty-state">Chargement...</div>';
    const violations = document.getElementById('violationsOnly').checked;
    try {
      const data = await TWM.api('/api/logs?violations=' + (violations ? '1' : '0') + '&limit=300');
      renderLogs(data.logs || []);
    } catch (e) {
      panel.innerHTML = '<div class="empty-state">Erreur : ' + TWM.escapeHtml(e.message) + '</div>';
    }
  }

  function renderLogs(lines) {
    const panel = document.getElementById('logsPanel');
    if (lines.length === 0) {
      panel.innerHTML = '<div class="empty-state">Aucune entree de journal.</div>';
      return;
    }
    panel.innerHTML = lines.map((line) => {
      const p = parseLogLine(line);
      const tag = p.result === 'denied' ? '<span class="tag tag-deny">[DENIED]</span>'
                : p.result === 'granted' ? '<span class="tag tag-allow">[GRANTED]</span>'
                : '<span class="tag tag-neutral">[?]</span>';
      return `<div class="log-row">
        <span class="log-ts">${TWM.escapeHtml(p.ts)}</span>
        ${tag}
        <span class="log-detail">${TWM.escapeHtml(p.detail)}</span>
      </div>`;
    }).join('');
  }

  document.getElementById('refreshLogsBtn').addEventListener('click', loadLogs);
  document.getElementById('violationsOnly').addEventListener('change', loadLogs);

  /* ==================================================================
     VIEW: HISTORY
     ================================================================== */

  function historyActionTag(action) {
    if (action === 'mode->learning')  return '<span class="tag tag-learn">[LEARNING]</span>';
    if (action === 'mode->enforcing') return '<span class="tag tag-allow">[ENFORCING]</span>';
    if (action === 'rule.add')        return '<span class="tag tag-allow">[RULE+]</span>';
    if (action === 'rule.delete')     return '<span class="tag tag-deny">[RULE-]</span>';
    if (action === 'learning.capture.start') return '<span class="tag tag-warn">[CAPTURE]</span>';
    return '<span class="tag tag-neutral">[' + TWM.escapeHtml(action || '?') + ']</span>';
  }

  async function loadHistory() {
    const panel = document.getElementById('historyPanel');
    panel.innerHTML = '<div class="empty-state">Chargement...</div>';
    try {
      const data = await TWM.api('/api/history');
      renderHistory((data.entries || []).slice().reverse());
    } catch (e) {
      panel.innerHTML = '<div class="empty-state">Erreur : ' + TWM.escapeHtml(e.message) + '</div>';
    }
  }

  function renderHistory(entries) {
    const panel = document.getElementById('historyPanel');
    if (entries.length === 0) {
      panel.innerHTML = '<div class="empty-state">Aucune modification enregistree.</div>';
      return;
    }
    panel.innerHTML = entries.map((e) => `
      <div class="log-row">
        <span class="log-ts">${TWM.escapeHtml(e.ts)}</span>
        ${historyActionTag(e.action)}
        <span class="log-detail">
          <span class="mono">${TWM.escapeHtml(e.domain)}</span>
          &nbsp;&middot;&nbsp;par ${TWM.escapeHtml(e.by)}
          ${e.detail ? '&nbsp;&middot;&nbsp;' + TWM.escapeHtml(e.detail) : ''}
        </span>
      </div>`).join('');
  }

  document.getElementById('refreshHistoryBtn').addEventListener('click', loadHistory);

  /* ---------------- go ---------------- */

  bootstrap();
})();
