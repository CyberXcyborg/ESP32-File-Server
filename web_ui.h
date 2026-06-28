/usr/bin/bash: warning: setlocale: LC_ALL: cannot change locale (en_US.UTF-8): No such file or directory
/usr/bin/bash: warning: setlocale: LC_ALL: cannot change locale (en_US.UTF-8): No such file or directory
#ifndef WEB_UI_H
#define WEB_UI_H

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 File Server v6.20</title>
<link rel="manifest" href="/manifest.json">
<meta name="theme-color" content="#0984e3">
<style>
:root{--bg:#f5f6fa;--card:#fff;--text:#2d3436;--text2:#636e72;--primary:#0984e3;--primary-dark:#0652DD;--danger:#d63031;--success:#00b894;--border:#dfe6e9;--shadow:0 2px 10px rgba(0,0,0,0.08);--radius:8px}
.dark{--bg:#1a1a2e;--card:#16213e;--text:#e0e0e0;--text2:#a0a0a0;--primary:#74b9ff;--primary-dark:#0984e3;--danger:#ff7675;--success:#55efc4;--border:#2d3748;--shadow:0 2px 10px rgba(0,0,0,0.3)}
.green{--bg:#e8f5e9;--card:#fff;--text:#1b5e20;--text2:#4caf50;--primary:#2e7d32;--primary-dark:#1b5e20;--danger:#c62828;--success:#43a047;--border:#c8e6c9;--shadow:0 2px 10px rgba(46,125,50,0.1);--radius:8px}
.purple{--bg:#f3e5f5;--card:#fff;--text:#4a148c;--text2:#7b1fa2;--primary:#6a1b9a;--primary-dark:#4a148c;--danger:#d32f2f;--success:#7b1fa2;--border:#e1bee7;--shadow:0 2px 10px rgba(106,27,154,0.1);--radius:8px}
.warm{--bg:#fff8e1;--card:#fff;--text:#5d4037;--text2:#8d6e63;--primary:#e65100;--primary-dark:#bf360c;--danger:#d32f2f;--success:#558b2f;--border:#ffe0b2;--shadow:0 2px 10px rgba(230,81,0,0.08);--radius:8px}
.midnight{--bg:#0d1117;--card:#161b22;--text:#c9d1d9;--text2:#8b949e;--primary:#58a6ff;--primary-dark:#1f6feb;--danger:#f85149;--success:#3fb950;--border:#30363d;--shadow:0 2px 10px rgba(0,0,0,0.4);--radius:8px}
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',system-ui,sans-serif;-webkit-tap-highlight-color:transparent}
body{background:var(--bg);color:var(--text);transition:background .3s,color .3s;line-height:1.5;overflow-x:hidden}
.container{max-width:1200px;margin:0 auto;padding:12px}
header{display:flex;justify-content:space-between;align-items:center;padding:12px 16px;background:var(--card);border-radius:var(--radius);box-shadow:var(--shadow);margin-bottom:12px;flex-wrap:wrap;gap:8px}
header h1{font-size:18px;color:var(--primary)}
.header-right{display:flex;align-items:center;gap:8px;font-size:13px;color:var(--text2);flex-wrap:wrap}
.header-right span{color:var(--primary);font-weight:600}
.btn{background:var(--primary);color:#fff;border:none;padding:10px 14px;border-radius:6px;cursor:pointer;font-size:13px;display:inline-flex;align-items:center;gap:5px;transition:all .2s;text-decoration:none;min-height:40px}
.btn:hover{background:var(--primary-dark)}
.btn-danger{background:var(--danger)}.btn-danger:hover{background:#c0392b}
.btn-ghost{background:transparent;color:var(--text);border:1px solid var(--border)}
.btn-ghost:hover{background:var(--border)}
.btn-sm{padding:6px 10px;font-size:12px;min-height:36px}
.search-box{display:flex;align-items:center;background:var(--bg);border:1px solid var(--border);border-radius:6px;padding:0 10px;gap:6px}
.search-box input{border:none;background:transparent;padding:8px 0;outline:none;color:var(--text);font-size:13px;width:140px}
.search-box input::placeholder{color:var(--text2)}
.controls{display:flex;gap:6px;margin-bottom:10px;flex-wrap:wrap;align-items:center}
.path-nav{display:flex;align-items:center;padding:8px 12px;background:var(--card);border-radius:var(--radius);margin-bottom:10px;overflow-x:auto;white-space:nowrap;font-size:13px;box-shadow:var(--shadow)}
.path-part{cursor:pointer;color:var(--primary);margin-right:4px;padding:4px 6px;border-radius:4px}
.path-part:hover{text-decoration:underline;background:var(--bg)}
.separator{margin:0 2px;color:var(--text2)}
.file-list{background:var(--card);border-radius:var(--radius);box-shadow:var(--shadow);overflow:hidden}
.file-list-header{display:grid;grid-template-columns:30px 1fr 90px 100px;background:var(--primary);color:#fff;padding:10px 12px;font-weight:600;font-size:13px;cursor:pointer;user-select:none}
.file-item{display:grid;grid-template-columns:30px 1fr 90px 100px;padding:10px 12px;border-bottom:1px solid var(--border);align-items:center;font-size:13px;transition:background .15s;min-height:44px}
.file-item:last-child{border-bottom:none}
.file-item:hover{background:var(--bg)}
.file-item.selected{background:#e3f2fd}
.dark .file-item.selected{background:#1e3a5f}
.file-icon{font-size:16px;text-align:center}
.file-name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;cursor:pointer;color:var(--text);padding:4px 0}
.file-name:hover{color:var(--primary)}
.file-size{text-align:right;color:var(--text2);font-size:12px}
.file-actions{display:flex;justify-content:flex-end;gap:4px}
.file-action{cursor:pointer;padding:6px 8px;border-radius:4px;transition:background .15s;font-size:14px;min-width:32px;min-height:32px;display:flex;align-items:center;justify-content:center}
.file-action:hover{background:var(--border)}
.empty-msg{text-align:center;padding:40px;color:var(--text2);font-size:14px}
.modal{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,.5);z-index:1000;justify-content:center;align-items:center;padding:10px}
.modal-content{background:var(--card);border-radius:var(--radius);width:100%;max-width:600px;max-height:90vh;overflow-y:auto;box-shadow:var(--shadow);animation:modalIn .2s ease}
.modal-header{padding:14px 18px;border-bottom:1px solid var(--border);display:flex;justify-content:space-between;align-items:center}
.modal-header h2{font-size:16px}
.close-modal{cursor:pointer;font-size:24px;color:var(--text2);padding:4px 8px;border-radius:4px;min-width:36px;min-height:36px;display:flex;align-items:center;justify-content:center}
.close-modal:hover{color:var(--danger);background:var(--border)}
.modal-body{padding:18px}
.form-group{margin-bottom:14px}
.form-group label{display:block;margin-bottom:4px;font-weight:500;font-size:13px}
.form-group input,.form-group select{width:100%;padding:10px;border:1px solid var(--border);border-radius:6px;font-size:14px;background:var(--bg);color:var(--text);min-height:40px}
.form-group input:focus{outline:none;border-color:var(--primary)}
.modal-footer{padding:14px 18px;border-top:1px solid var(--border);display:flex;justify-content:flex-end;gap:8px;flex-wrap:wrap}
.progress-bar{width:100%;height:6px;background:var(--border);border-radius:3px;margin-top:10px;overflow:hidden;display:none}
.progress-bar-fill{height:100%;background:var(--primary);width:0;transition:width .3s;border-radius:3px}
.toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);padding:12px 20px;border-radius:6px;color:#fff;font-size:13px;z-index:2000;opacity:0;transition:opacity .3s;max-width:90vw;text-align:center;box-shadow:var(--shadow)}
.toast.show{opacity:1}.toast.success{background:var(--success)}.toast.error{background:var(--danger)}.toast.info{background:var(--primary)}
.preview-container{text-align:center;padding:10px}
.preview-container img{max-width:100%;max-height:60vh;border-radius:6px}
.preview-container pre{text-align:left;background:var(--bg);padding:15px;border-radius:6px;overflow-x:auto;font-size:12px;max-height:60vh;overflow-y:auto;white-space:pre-wrap;word-break:break-all}
.preview-container audio,.preview-container video{width:100%;max-height:60vh}
.drop-zone{border:2px dashed var(--border);border-radius:var(--radius);padding:24px;text-align:center;margin-bottom:10px;transition:all .2s;cursor:pointer;background:var(--card);min-height:80px;display:flex;flex-direction:column;align-items:center;justify-content:center}
.drop-zone.dragover{border-color:var(--primary);background:#e3f2fd}
.dark .drop-zone.dragover{background:#1e3a5f}
.filter-bar{display:flex;gap:6px;padding:6px 0;flex-wrap:wrap}
.filter-chip{padding:4px 12px;border-radius:20px;font-size:12px;cursor:pointer;border:1px solid var(--border);background:var(--card);color:var(--text2);transition:all .15s;user-select:none}
.filter-chip:hover{border-color:var(--primary);color:var(--primary)}
.filter-chip.active{background:var(--primary);color:#fff;border-color:var(--primary)}
.drop-zone p{color:var(--text2);font-size:13px;margin-top:6px}
.storage-bar{height:6px;background:var(--border);border-radius:3px;overflow:hidden;margin-top:4px}
.storage-bar-fill{height:100%;background:var(--primary);border-radius:3px;transition:width .3s}
.storage-info{font-size:11px;color:var(--text2);margin-top:2px}
.ctx-menu{display:none;position:fixed;background:var(--card);border:1px solid var(--border);border-radius:var(--radius);box-shadow:var(--shadow);z-index:3000;min-width:180px;overflow:hidden}
.ctx-menu.show{display:block}
kbd{font-family:monospace;font-size:12px}
.ctx-item{padding:12px 16px;cursor:pointer;font-size:13px;display:flex;align-items:center;gap:8px;transition:background .15s;min-height:44px}
.ctx-item:hover{background:var(--bg)}
.ctx-sep{height:1px;background:var(--border);margin:4px 0}
.share-link{display:flex;gap:8px;margin-top:10px}
.share-link input{flex:1;background:var(--bg);border:1px solid var(--border);padding:10px;border-radius:6px;font-size:13px;color:var(--text)}
.log-table{width:100%;border-collapse:collapse;font-size:12px}
.log-table th,.log-table td{padding:8px 10px;text-align:left;border-bottom:1px solid var(--border)}
.log-table th{background:var(--primary);color:#fff;position:sticky;top:0}
.log-table tr:hover{background:var(--bg)}
.log-action{display:inline-block;padding:2px 8px;border-radius:4px;font-size:11px;font-weight:600;color:#fff}
.log-upload{background:var(--success)}.log-delete{background:var(--danger)}.log-rename{background:#6c5ce7}
.log-download{background:var(--primary)}.log-share{background:#fdcb6e;color:#333}.log-mkdir{background:#00cec9}
.log-move{background:#a29bfe}.log-copy{background:#74b9ff}
.nav-tabs{display:flex;gap:4px;margin-bottom:10px;background:var(--card);border-radius:var(--radius);padding:4px;box-shadow:var(--shadow);overflow-x:auto}
.nav-tab{padding:10px 14px;border-radius:6px;cursor:pointer;font-size:13px;font-weight:500;color:var(--text2);transition:all .2s;border:none;background:transparent;white-space:nowrap;min-height:40px}
.nav-tab.active{background:var(--primary);color:#fff}
.nav-tab:hover:not(.active){background:var(--bg)}
.view{display:none}.view.active{display:block}
/* File info panel */
.info-panel{background:var(--card);border-radius:var(--radius);box-shadow:var(--shadow);padding:16px;margin-bottom:10px;font-size:13px;display:none}
.info-panel.show{display:block}
.info-panel h3{font-size:14px;margin-bottom:10px;color:var(--primary)}
.info-row{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid var(--border)}
.info-row:last-child{border-bottom:none}
.info-label{color:var(--text2)}
/* Settings */
.settings-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.settings-section{background:var(--card);border-radius:var(--radius);box-shadow:var(--shadow);padding:16px;margin-bottom:12px}
.settings-section h3{font-size:14px;margin-bottom:12px;color:var(--primary);padding-bottom:8px;border-bottom:1px solid var(--border)}
/* Folder tree for move dialog */
.folder-tree{max-height:300px;overflow-y:auto;border:1px solid var(--border);border-radius:6px;padding:8px;background:var(--bg)}
.folder-tree-item{padding:6px 8px;cursor:pointer;border-radius:4px;font-size:13px;display:flex;align-items:center;gap:6px;min-height:36px}
.folder-tree-item:hover{background:var(--border)}
.folder-tree-item.selected{background:#e3f2fd}
@keyframes modalIn{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}
@media(max-width:768px){
  .file-list-header,.file-item{grid-template-columns:28px 1fr 80px}
  .file-size{display:none}
  .search-box input{width:80px}
  header{flex-direction:column;align-items:flex-start}
  .settings-grid{grid-template-columns:1fr}
  .toast{bottom:10px;left:10px;right:10px;transform:none;max-width:none}
  .controls{flex-wrap:wrap}
  #copySelBtn,#moveSelBtn,#renameSelBtn{display:none !important}
}
/* Loading spinner */
.loading-spinner{display:flex;justify-content:center;align-items:center;padding:40px}
.loading-spinner::after{content:'';display:block;width:32px;height:32px;border:3px solid var(--border);border-top-color:var(--primary);border-radius:50%;animation:spin 0.8s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
</style>
</head>
<body>
<!-- Global drag overlay -->
<div id="globalDragOverlay" style="display:none;position:fixed;inset:0;background:rgba(9,132,227,0.15);border:3px dashed var(--primary);z-index:9999;align-items:center;justify-content:center;pointer-events:none">
  <div style="background:var(--card);padding:32px 48px;border-radius:16px;box-shadow:var(--shadow);text-align:center">
    <div style="font-size:48px;margin-bottom:8px">📥</div>
    <div style="font-size:18px;font-weight:600;color:var(--text)">Drop files to upload</div>
  </div>
</div>
<div class="container">
  <header>
    <h1>📁 ESP32 File Server <small style="font-size:11px;color:var(--text2)">v6.20</small></h1>
    <div class="header-right">
      <span id="userDisplay"></span>
      <div class="search-box">🔍<input type="text" id="searchInput" placeholder="Search..." oninput="filterFiles()"></div>
      <button class="btn btn-ghost btn-sm" id="themeBtn" onclick="showThemeMenu()" title="Change theme">🎨</button>
      <button class="btn btn-ghost btn-sm" id="darkToggle" onclick="toggleDark()">🌙</button>
      <button class="btn btn-ghost btn-sm" onclick="showRecentFiles()" title="Recent files">🕐</button>
      <button class="btn btn-ghost btn-sm" onclick="showShortcuts()" title="Keyboard shortcuts (?)">⌨️</button>
      <a href="/logout" class="btn btn-sm">Logout</a>
    </div>
  </header>

  <!-- Theme Menu -->
  <div class="ctx-menu" id="themeMenu" style="min-width:160px">
    <div class="ctx-item" onclick="setTheme('')">☀️ Default</div>
    <div class="ctx-item" onclick="setTheme('dark')">🌙 Dark</div>
    <div class="ctx-item" onclick="setTheme('green')">🌿 Green</div>
    <div class="ctx-item" onclick="setTheme('purple')">💜 Purple</div>
    <div class="ctx-item" onclick="setTheme('warm')">🔥 Warm</div>
    <div class="ctx-item" onclick="setTheme('midnight')">🌌 Midnight</div>
  </div>

  <div class="nav-tabs">
    <button class="nav-tab active" onclick="switchView('files',this)">📂 Files</button>
    <button class="nav-tab" onclick="switchView('trash',this)">🗑️ Trash</button>
    <button class="nav-tab admin-only" onclick="switchView('users',this)">👥 Users</button>
    <button class="nav-tab admin-only" onclick="switchView('settings',this)">⚙️ Settings</button>
    <button class="nav-tab admin-only" onclick="switchView('apikeys',this)">🔑 API Keys</button>
    <button class="nav-tab admin-only" onclick="switchView('log',this)">📋 Activity</button>
    <button class="nav-tab" onclick="switchView('analytics',this)">📊 Analytics</button>
  </div>

  <!-- FILES VIEW -->
  <div id="filesView" class="view active">
    <div class="controls">
      <button class="btn" onclick="showUploadModal()">⬆️ Upload</button>
      <button class="btn" onclick="showNewFolderModal()">📁 New Folder</button>
      <button class="btn" onclick="showNewFileModal()">📄 New File</button>
      <button class="btn" onclick="refreshFiles()">🔄 Refresh</button>
      <button class="btn" onclick="toggleSelectAll()" id="selectAllBtn">☑️ Select All</button>
      <button class="btn btn-ghost" onclick="selectInverse()" id="selectInverseBtn" style="display:none">🔄 Invert</button>
      <button class="btn" onclick="downloadZip()">📦 Download ZIP</button>
      <button class="btn btn-ghost" onclick="exportFileList()" title="Export file list">📋 Export</button>
      <button class="btn btn-danger" id="delSelBtn" style="display:none" onclick="deleteSelected()">🗑️ Delete Selected</button>
      <button class="btn" id="copySelBtn" style="display:none" onclick="copySelected()">📋 Copy Selected</button>
      <button class="btn" id="moveSelBtn" style="display:none" onclick="moveSelected()">📦 Move Selected</button>
      <button class="btn" id="renameSelBtn" style="display:none" onclick="renameSelected()">✏️ Rename Selected</button>
      <button class="btn" id="downloadSelBtn" style="display:none" onclick="downloadSelected()">⬇️ Download Selected</button>
      <select class="btn btn-ghost" id="sortSelect" onchange="sortFiles()" style="padding:8px 12px">
        <option value="name-asc">Name ↑</option>
        <option value="name-desc">Name ↓</option>
        <option value="size-asc">Size ↑</option>
        <option value="size-desc">Size ↓</option>
        <option value="date-asc">Date ↑</option>
        <option value="date-desc">Date ↓</option>
      </select>
      <div style="flex:1"></div>
      <div class="storage-info" id="storageInfo"></div>
    </div>
    <div class="path-nav" id="pathNav"><span class="path-part">Root</span></div>
    <div class="filter-bar" id="filterBar">
      <span class="filter-chip active" onclick="setTypeFilter('all',this)">📁 All</span>
      <span class="filter-chip" onclick="setTypeFilter('images',this)">🖼️ Images</span>
      <span class="filter-chip" onclick="setTypeFilter('video',this)">🎬 Video</span>
      <span class="filter-chip" onclick="setTypeFilter('audio',this)">🎵 Audio</span>
      <span class="filter-chip" onclick="setTypeFilter('docs',this)">📄 Docs</span>
      <span class="filter-chip" onclick="setTypeFilter('code',this)">💻 Code</span>
      <span class="filter-chip" onclick="setTypeFilter('archives',this)">📦 Archives</span>
    </div>
    <div id="fileCountBadge" style="font-size:11px;color:var(--text2);padding:4px 0"></div>
    <div id="infoPanel" class="info-panel">
      <h3>📄 <span id="infoName"></span></h3>
      <div class="info-row"><span class="info-label">Type</span><span id="infoType"></span></div>
      <div class="info-row"><span class="info-label">Size</span><span id="infoSize"></span></div>
      <div class="info-row"><span class="info-label">Path</span><span id="infoPath" style="word-break:break-all;font-size:11px"></span></div>
    </div>
    <div class="file-list">
      <div class="file-list-header">
        <div></div>
        <div onclick="setSort('name')">Name</div>
        <div onclick="setSort('date')" style="text-align:center;min-width:70px">Modified</div>
        <div onclick="setSort('size')" style="text-align:right">Size</div>
        <div style="text-align:right">Actions</div>
      </div>
      <div id="fileContainer"><div class="empty-msg">Loading...</div></div>
    </div>
  </div>

  <!-- TRASH VIEW -->
  <div id="trashView" class="view">
    <div class="controls">
      <button class="btn btn-danger" onclick="emptyTrash()">❌ Empty Trash</button>
      <button class="btn" id="restoreSelBtn" style="display:none" onclick="restoreSelected()">♻️ Restore Selected</button>
      <button class="btn" onclick="loadTrash()">🔄 Refresh</button>
    </div>
    <div class="file-list">
      <div class="file-list-header"><div></div><div>Name</div><div>Size</div><div style="text-align:right">Actions</div></div>
      <div id="trashContainer"><div class="empty-msg">Loading...</div></div>
    </div>
  </div>

  <!-- USERS VIEW -->
  <div id="usersView" class="view">
    <div class="controls"><button class="btn" onclick="showUserModal()">+ Add User</button></div>
    <div style="background:var(--card);border-radius:var(--radius);box-shadow:var(--shadow);overflow:hidden">
      <table style="width:100%;border-collapse:collapse">
        <thead><tr style="background:var(--primary);color:#fff"><th style="padding:10px 15px;text-align:left;font-size:13px">Username</th><th style="padding:10px 15px;text-align:left;font-size:13px">Role</th><th style="padding:10px 15px;text-align:right;font-size:13px">Actions</th></tr></thead>
        <tbody id="userTable"></tbody>
      </table>
    </div>
  </div>

  <!-- SETTINGS VIEW -->
  <div id="settingsView" class="view">
    <div class="settings-section">
      <h3>📡 WiFi Settings</h3>
      <div class="settings-grid">
        <div class="form-group"><label>WiFi SSID</label><input type="text" id="setWifiSsid"></div>
        <div class="form-group"><label>WiFi Password</label><input type="password" id="setWifiPass"></div>
      </div>
    </div>
    <div class="settings-section">
      <h3>📶 Access Point Settings</h3>
      <div class="settings-grid">
        <div class="form-group"><label>AP SSID</label><input type="text" id="setApSsid"></div>
        <div class="form-group"><label>AP Password</label><input type="password" id="setApPass"></div>
      </div>
    </div>
    <div class="settings-section">
      <h3>🔑 FTP Settings</h3>
      <div class="settings-grid">
        <div class="form-group"><label>FTP Username</label><input type="text" id="setFtpUser"></div>
        <div class="form-group"><label>FTP Password</label><input type="password" id="setFtpPass"></div>
      </div>
    </div>
    <div class="settings-section">
      <h3>ℹ️ System Info</h3>
      <div class="info-row"><span class="info-label">Version</span><span id="sysVersion"></span></div>
      <div class="info-row"><span class="info-label">IP Address</span><span id="sysIp"></span></div>
      <div class="info-row"><span class="info-label">Mode</span><span id="sysMode"></span></div>
      <div class="info-row"><span class="info-label">Uptime</span><span id="sysUptime"></span></div>
      <div class="info-row"><span class="info-label">Free Memory</span><span id="sysHeap"></span></div>
    </div>
    <div class="settings-section">
      <h3>📦 Storage Quotas</h3>
      <div id="quotaList" style="margin-bottom:8px;color:var(--text2);font-size:13px">Loading...</div>
      <div class="settings-grid">
        <div class="form-group"><label>Username</label><input type="text" id="quotaUser" placeholder="e.g. user1"></div>
        <div class="form-group"><label>Limit (bytes, 0=unlimited)</label><input type="number" id="quotaLimit" min="0" value="0" placeholder="e.g. 104857600 for 100MB"></div>
      </div>
      <button class="btn btn-sm" onclick="setQuota()">Set Quota</button>
    </div>
    <div class="settings-section">
      <h3>🌐 Server Settings</h3>
      <div class="settings-grid">
        <div class="form-group"><label>Web Server Port</label><input type="number" id="setWebPort" min="80" max="65535" value="80"></div>
      </div>
    </div>
    <div style="margin-top:12px">
      <button class="btn" onclick="saveSettings()">💾 Save Settings</button>
      <button class="btn btn-ghost" onclick="loadSettings()">🔄 Refresh</button>
    </div>
  </div>

  <!-- API KEYS VIEW -->
  <div id="apiKeysView" class="view">
    <div class="settings-section" style="margin-top:10px">
      <h3>🔑 API Keys</h3>
      <p style="font-size:13px;color:var(--text2);margin-bottom:10px">API keys allow programmatic access without a browser session. Use the <code>X-API-Key</code> header or <code>api_key</code> query parameter.</p>
      <div id="apiKeyList" style="margin-bottom:10px"><p style="color:var(--text2)">Loading...</p></div>
      <button class="btn btn-sm" onclick="createApiKey()">+ Generate New Key</button>
    </div>
  </div>

  <!-- ACTIVITY LOG VIEW -->
  <div id="logView" class="view">
    <div class="controls"><button class="btn" onclick="loadLog()">🔄 Refresh</button></div>
    <div style="background:var(--card);border-radius:var(--radius);box-shadow:var(--shadow);overflow:auto;max-height:70vh">
      <table class="log-table"><thead><tr><th>Time</th><th>User</th><th>Action</th><th>Path</th></tr></thead><tbody id="logTable"></tbody></table>
    </div>
  </div>
  <div id="analyticsView" class="view">
    <div class="controls"><button class="btn" onclick="loadAnalytics()">🔄 Refresh</button></div>
    <div class="settings-grid" style="grid-template-columns:1fr 1fr;align-items:start">
      <div class="info-panel show" style="margin:0">
        <h3 style="margin-bottom:12px">📊 Storage Breakdown</h3>
        <div id="analyticsBreakdown"><p style="color:var(--text2)">Loading...</p></div>
      </div>
      <div class="info-panel show" style="margin:0">
        <h3 style="margin-bottom:12px">💾 SD Card Health</h3>
        <div id="analyticsHealth"><p style="color:var(--text2)">Loading...</p></div>
        <div id="wearBar" style="margin-top:8px"><div class="progress-bar" style="height:10px"><div id="wearFill" style="height:100%;background:var(--primary);width:0%;border-radius:5px;transition:width .5s"></div></div></div>
      </div>
    </div>
    <div class="info-panel show" style="margin-top:10px" id="analyticsCharts">
      <h3 style="margin-bottom:12px">📈 File Type Distribution</h3>
      <div id="typeBars"></div>
    </div>
    <div class="info-panel show" style="margin-top:10px">
      <h3 style="margin-bottom:12px">🔍 Duplicate Files</h3>
      <div id="dupResults"><p style="color:var(--text2)">Click Refresh to scan for duplicates</p></div>
    </div>
    <div class="info-panel show" style="margin-top:10px">
      <h3 style="margin-bottom:12px">🔒 Security Audit Log</h3>
      <div id="auditLog"><p style="color:var(--text2)">Click Refresh to view audit log</p></div>
    </div>
  </div>
</div>

<!-- Context Menu -->
<div class="ctx-menu" id="ctxMenu">
  <div class="ctx-item" onclick="ctxOpen()">📂 Open</div>
  <div class="ctx-item" onclick="ctxPreview()">👁️ Preview</div>
  <div class="ctx-item" onclick="ctxInfo()">ℹ️ Info</div>
  <div class="ctx-item" onclick="ctxCRC()">🔢 CRC32</div>
  <div class="ctx-item" onclick="ctxDownload()">⬇️ Download</div>
  <div class="ctx-sep"></div>
  <div class="ctx-item" onclick="ctxRename()">✏️ Rename</div>
  <div class="ctx-item" onclick="ctxMove()">📦 Move</div>
  <div class="ctx-item" onclick="ctxCopy()">📋 Copy</div>
  <div class="ctx-item" onclick="ctxShare()">🔗 Share</div>
  <div class="ctx-item" onclick="ctxCopyPath()">📋 Copy Path</div>
  <div class="ctx-item" onclick="ctxLock()">🔒 Lock/Unlock</div>
  <div class="ctx-item" onclick="ctxCompress()">📦 Compress (ZIP)</div>
  <div class="ctx-sep"></div>
  <div class="ctx-item" onclick="ctxDelete()">🗑️ Delete</div>
  <div class="ctx-sep"></div>
  <div class="ctx-item" onclick="ctxSelectSameType()">☑️ Select All Same Type</div>
</div>

<!-- Recent Files Modal -->
<div class="modal" id="recentModal">
  <div class="modal-content" style="max-width:600px">
    <div class="modal-header"><h2 id="recentTitle">🕐 Recent Files</h2><span class="close-modal" onclick="closeModal('recentModal')">&times;</span></div>
    <div class="modal-body" id="recentList" style="max-height:60vh;overflow-y:auto;padding:0"></div>
    <div class="modal-footer"><button class="btn btn-ghost" onclick="closeModal('recentModal')">Close</button></div>
  </div>
</div>

<!-- Modals -->
<div class="modal" id="uploadModal">
  <div class="modal-content">
    <div class="modal-header"><h2>Upload Files</h2><span class="close-modal" onclick="closeModal('uploadModal')">&times;</span></div>
    <div class="modal-body">
      <div class="drop-zone" id="dropZone" onclick="document.getElementById('fileInput').click()">
        <div style="font-size:28px">📤</div>
        <p>Drag & drop files/folders here or click to browse</p>
        <input type="file" id="fileInput" multiple webkitdirectory style="display:none" onchange="handleFiles(this.files)">
      </div>
      <div id="uploadList"></div>
      <div class="progress-bar" id="uploadProgress"><div class="progress-bar-fill" id="uploadProgressFill"></div></div>
    </div>
    <div class="modal-footer"><button class="btn btn-ghost" onclick="closeModal('uploadModal')">Close</button></div>
  </div>
</div>

<div class="modal" id="folderModal">
  <div class="modal-content">
    <div class="modal-header"><h2>Create New Folder</h2><span class="close-modal" onclick="closeModal('folderModal')">&times;</span></div>
    <div class="modal-body"><div class="form-group"><label>Folder Name</label><input type="text" id="folderName" placeholder="Enter folder name"></div></div>
    <div class="modal-footer"><button class="btn btn-ghost" onclick="closeModal('folderModal')">Cancel</button><button class="btn" onclick="createFolder()">Create</button></div>
</div>
<div class="modal" id="newFileModal">
    <div class="modal-header"><h2>Create New File</h2><span class="close-modal" onclick="closeModal('newFileModal')">&times;</span></div>
    <div class="modal-body"><div class="form-group"><label>File Name</label><input type="text" id="newFileName" placeholder="Enter file name (e.g. notes.txt)"></div></div>
    <div class="modal-footer"><button class="btn btn-ghost" onclick="closeModal('newFileModal')">Cancel</button><button class="btn" onclick="createFile()">Create</button></div>
</div>
</div>

<div class="modal" id="renameModal">
  <div class="modal-content">
    <div class="modal-header"><h2>Rename</h2><span class="close-modal" onclick="closeModal('renameModal')">&times;</span></div>
    <div class="modal-body"><div class="form-group"><label>New Name</label><input type="text" id="renameInput"></div><input type="hidden" id="renamePath"></div>
    <div class="modal-footer"><button class="btn btn-ghost" onclick="closeModal('renameModal')">Cancel</button><button class="btn" onclick="doRename()">Rename</button></div>
  </div>
</div>

<!-- Move/Copy Modal -->
<div class="modal" id="moveModal">
  <div class="modal-content">
    <div class="modal-header"><h2 id="moveModalTitle">Move</h2><span class="close-modal" onclick="closeModal('moveModal')">&times;</span></div>
    <div class="modal-body">
      <p style="font-size:13px;color:var(--text2);margin-bottom:10px">Select destination folder:</p>
      <div class="folder-tree" id="folderTree"></div>
      <input type="hidden" id="moveSrcPath">
      <input type="hidden" id="moveMode" value="move">
    </div>
    <div class="modal-footer"><button class="btn btn-ghost" onclick="closeModal('moveModal')">Cancel</button><button class="btn" onclick="doMoveCopy()">Move Here</button></div>
  </div>
</div>

<div class="modal" id="previewModal">
  <div class="modal-content" style="max-width:900px">
    <div class="modal-header"><h2 id="previewTitle">Preview</h2><span class="close-modal" onclick="closeModal('previewModal')">&times;</span></div>
    <div class="modal-body"><div class="preview-container" id="previewContent"></div></div>
  </div>
</div>

<div class="modal" id="shareModal">
  <div class="modal-content" style="max-width:500px">
    <div class="modal-header"><h2>🔗 Share Link</h2><span class="close-modal" onclick="closeModal('shareModal')">&times;</span></div>
    <div class="modal-body">
      <p style="font-size:13px;color:var(--text2);margin-bottom:12px">Anyone with this link can download the file.</p>
      <div class="share-link"><input type="text" id="shareUrl" readonly><button class="btn" onclick="copyShareUrl()">📋 Copy</button></div>
      <div style="display:flex;gap:8px;margin-top:8px;flex-wrap:wrap">
        <button class="btn btn-sm btn-ghost" onclick="copyShareUrl()">📋 Copy Link</button>
        <button class="btn btn-sm btn-ghost" onclick="shareViaEmail()">✉️ Email</button>
      </div>
      <p style="margin-top:10px;font-size:12px;color:var(--text2)">⏱️ Link expires in: <span id="shareExpiry">24h</span></p>
    </div>
  </div>
</div>

<div class="modal" id="userModal">
  <div class="modal-content">
    <div class="modal-header"><h2 id="userModalTitle">Add User</h2><span class="close-modal" onclick="closeModal('userModal')">&times;</span></div>
    <div class="modal-body">
      <div class="form-group"><label>Username</label><input type="text" id="uName"></div>
      <div class="form-group"><label>Password</label><input type="password" id="uPass"></div>
      <div class="form-group"><label>Role</label><select id="uRole"><option value="user">User</option><option value="admin">Admin</option></select></div>
    </div>
    <div class="modal-footer"><button class="btn btn-ghost" onclick="closeModal('userModal')">Cancel</button><button class="btn" onclick="saveUser()">Save</button></div>
  </div>
</div>

<div class="modal" id="confirmModal">
  <div class="modal-content" style="max-width:400px">
    <div class="modal-header"><h2 id="confirmTitle">Confirm</h2><span class="close-modal" onclick="closeModal('confirmModal')">&times;</span></div>
    <div class="modal-body"><p id="confirmMsg"></p></div>
    <div class="modal-footer"><button class="btn btn-ghost" onclick="closeModal('confirmModal')">Cancel</button><button class="btn btn-danger" id="confirmBtn">Delete</button></div>
  </div>
</div>

<!-- Shortcuts Modal -->
<div class="modal" id="shortcutsModal">
  <div class="modal-content" style="max-width:500px">
    <div class="modal-header"><h2>⌨️ Keyboard Shortcuts</h2><span class="close-modal" onclick="closeModal('shortcutsModal')">&times;</span></div>
    <div class="modal-body">
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;font-size:13px">
        <div style="padding:8px;background:var(--bg);border-radius:6px"><kbd style="background:var(--card);padding:2px 8px;border-radius:4px;border:1px solid var(--border)">Ctrl+U</kbd> Upload</div>
        <div style="padding:8px;background:var(--bg);border-radius:6px"><kbd style="background:var(--card);padding:2px 8px;border-radius:4px;border:1px solid var(--border)">Ctrl+N</kbd> New folder</div>
        <div style="padding:8px;background:var(--bg);border-radius:6px"><kbd style="background:var(--card);padding:2px 8px;border-radius:4px;border:1px solid var(--border)">Ctrl+A</kbd> Select all</div>
        <div style="padding:8px;background:var(--bg);border-radius:6px"><kbd style="background:var(--card);padding:2px 8px;border-radius:4px;border:1px solid var(--border)">Esc</kbd> Close modal</div>
        <div style="padding:8px;background:var(--bg);border-radius:6px"><kbd style="background:var(--card);padding:2px 8px;border-radius:4px;border:1px solid var(--border)">Del</kbd> Delete selected</div>
        <div style="padding:8px;background:var(--bg);border-radius:6px"><kbd style="background:var(--card);padding:2px 8px;border-radius:4px;border:1px solid var(--border">F5</kbd> Refresh</div>
        <div style="padding:8px;background:var(--bg);border-radius:6px"><kbd style="background:var(--card);padding:2px 8px;border-radius:4px;border:1px solid var(--border)">?</kbd> This help</div>
        <div style="padding:8px;background:var(--bg);border-radius:6px"><kbd style="background:var(--card);padding:2px 8px;border-radius:4px;border:1px solid var(--border)">←</kbd> Go back</div>
        <div style="padding:8px;background:var(--bg);border-radius:6px"><kbd style="background:var(--card);padding:2px 8px;border-radius:4px;border:1px solid var(--border)">Ctrl+K</kbd> Search</div>
        <div style="padding:8px;background:var(--bg);border-radius:6px"><kbd style="background:var(--card);padding:2px 8px;border-radius:4px;border:1px solid var(--border)">Ctrl+R</kbd> Batch rename selected</div>
        <div style="padding:8px;background:var(--bg);border-radius:6px"><kbd style="background:var(--card);padding:2px 8px;border-radius:4px;border:1px solid var(--border)">F2</kbd> Rename</div>
      </div>
    </div>
  </div>
</div>

<div class="toast" id="toast"></div>

<script>
let currentPath='/',files=[],selectedFiles=[],userLevel='user',sortBy='name',sortAsc=true,trashFiles=[],users=[],ctxTarget=null,csrfToken='';
// Restore sort preference from localStorage
(function(){
  const s=localStorage.getItem('sort_by');
  const o=localStorage.getItem('sort_asc');
  if(s){sortBy=s;sortAsc=o!=='false';}
})();
const token=getToken();

// ============== FETCH WITH RETRY =============
// Auto-retry failed API calls up to 2 times with 1s backoff (WiFi resilience)
function fetchRetry(url,opts,retries=2,delay=1000){
  return fetch(url,opts).catch(err=>{
    if(retries<=0)throw err;
    return new Promise(r=>setTimeout(r,delay)).then(()=>fetchRetry(url,opts,retries-1,delay*2));
  });
}

// Fetch CSRF token for this session
function fetchCsrf(){
  fetchRetry('/api/csrf',{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(d=>{if(d.csrf)csrfToken=d.csrf;}).catch(()=>{});
}

document.addEventListener('DOMContentLoaded',()=>{
  loadTheme();
  initWebSocket();
  fetchCsrf();
  loadFiles('/');
  initFavorites();
  const dz=document.getElementById('dropZone');
  dz.addEventListener('dragover',e=>{e.preventDefault();dz.classList.add('dragover');});
  dz.addEventListener('dragleave',()=>dz.classList.remove('dragover'));
  dz.addEventListener('drop',handleDrop);
  window.onclick=e=>{if(e.target.classList.contains('modal'))e.target.style.display='none';hideCtxMenu();hideThemeMenu();};
  document.getElementById('folderName').addEventListener('keydown',e=>{if(e.key==='Enter')createFolder();});
  document.getElementById('newFileName').addEventListener('keydown',e=>{if(e.key==='Enter')createFile();});
  // Keyboard shortcuts
  document.addEventListener('keydown',e=>{
    if(e.key==='Escape'){document.querySelectorAll('.modal').forEach(m=>m.style.display='none');hideCtxMenu();hideThemeMenu();selectedFiles=[];updateSelBtn();renderFiles();}
    if(e.key==='Delete'&&selectedFiles.length>0&&document.activeElement.tagName!=='INPUT'){e.preventDefault();deleteSelected();}
    if(e.key==='F2'&&selectedFiles.length===1){e.preventDefault();const f=files.find(x=>x.path===selectedFiles[0]);if(f)showRenameModal(f.path,f.name);}
    if(e.key==='ArrowLeft'&&document.activeElement.tagName!=='INPUT'&&document.activeElement.tagName!=='TEXTAREA'){e.preventDefault();if(currentPath!=='/'){const p=currentPath.substring(0,currentPath.lastIndexOf('/'));loadFiles(p?p:'/');}}
    if(e.ctrlKey&&e.key==='a'&&document.activeElement.tagName!=='INPUT'){e.preventDefault();selectedFiles=files.map(f=>f.path);updateSelBtn();renderFiles();}
    if(e.ctrlKey&&e.key==='k'&&document.activeElement.tagName!=='INPUT'){e.preventDefault();document.getElementById('searchInput').focus();document.getElementById('searchInput').select();}
    if(e.ctrlKey&&e.key==='r'&&selectedFiles.length>0&&document.activeElement.tagName!=='INPUT'){e.preventDefault();renameSelected();}
    if(e.key==='?'&&document.activeElement.tagName!=='INPUT'){e.preventDefault();showShortcuts();}
  });
  // ============== CLIPBOARD PASTE UPLOAD ==============
  // Paste images or files from clipboard directly into the file browser
  document.addEventListener('paste',e=>{
    if(document.activeElement.tagName==='INPUT'||document.activeElement.tagName==='TEXTAREA')return;
    const items=e.clipboardData&&e.clipboardData.items;
    if(!items)return;
    let hasFiles=false;
    const pasteFiles=[];
    for(let i=0;i<items.length;i++){
      if(items[i].kind==='file'){
        const f=items[i].getAsFile();
        if(f&&f.size>0){hasFiles=true;pasteFiles.push(f);}
      }
    }
    if(!hasFiles)return;
    e.preventDefault();
    showToast('Uploading pasted file(s)...','info');
    // Reuse existing upload logic
    uploadQueue=[];uploadCompleted=0;uploadFailed=0;
    const MAX_SIZE=16*1024*1024;
    pasteFiles.forEach(f=>{
      if(f.size>MAX_SIZE){showToast(f.name+' exceeds 16MB limit','error');return;}
      uploadQueue.push({file:f,path:f.name||('clipboard-'+Date.now()+'.png')});
    });
    openModal('uploadModal');
    const list=document.getElementById('uploadList');list.innerHTML='';
    uploadQueue.forEach(item=>{
      const id='ul-'+item.path.replace(/[^a-zA-Z0-9]/g,'_');
      list.innerHTML+=`<div style="display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid var(--border);font-size:12px"><span>${item.path}</span><span id="${id}">⏳</span></div>`;
    });
    startUploadQueue();
  });
});

function getToken(){
  const p=new URLSearchParams(window.location.search);const t=p.get('token');
  if(t){document.cookie='session_token='+t+';path=/;max-age=1800';return t;}

// Global drag-and-drop: show overlay when files dragged anywhere on page
let gDragCounter=0;
document.addEventListener('dragenter',e=>{e.preventDefault();gDragCounter++;document.getElementById('globalDragOverlay').style.display='flex';});
document.addEventListener('dragleave',e=>{e.preventDefault();gDragCounter--;if(gDragCounter<=0){gDragCounter=0;document.getElementById('globalDragOverlay').style.display='none';}});
document.addEventListener('dragover',e=>e.preventDefault());
document.addEventListener('drop',e=>{e.preventDefault();gDragCounter=0;document.getElementById('globalDragOverlay').style.display='none';handleFiles(e.dataTransfer.files);});
  const c=document.cookie.split(';');
  for(let x of c){const[n,v]=x.trim().split('=');if(n==='session_token')return v;}
  return '';
}
// ============== THEME SYSTEM ==============
function setTheme(name){
  document.body.className=name;
  localStorage.setItem('theme',name);
  hideThemeMenu();
  hideCtxMenu();
}
function loadTheme(){
  let t=localStorage.getItem('theme')||'';
  // Auto-detect system preference if no saved theme
  if(!t && window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches){
    t='dark';
  }
  document.body.className=t;
  if(t==='dark')document.getElementById('darkToggle').textContent='☀️';
  // Listen for system theme changes
  if(window.matchMedia){
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change',e=>{
      if(!localStorage.getItem('theme')){
        document.body.className=e.matches?'dark':'';
        document.getElementById('darkToggle').textContent=e.matches?'☀️':'🌙';
      }
    });
  }
}
function showThemeMenu(){
  const m=document.getElementById('themeMenu');
  const btn=document.getElementById('themeBtn');
  const r=btn.getBoundingClientRect();
  m.style.display='block';
  m.style.left=Math.min(r.left,window.innerWidth-180)+'px';
  m.style.top=r.bottom+4+'px';
}
function getStorageColor(pct){return pct>90?'danger':pct>75?'warning':'ok';}
function hideThemeMenu(){document.getElementById('themeMenu').style.display='none';}
function toggleDark(){
  const isDark=document.body.classList.toggle('dark');
  localStorage.setItem('theme',isDark?'dark':'');
  document.getElementById('darkToggle').textContent=isDark?'☀️':'🌙';
}
// ============== WEBSOCKET ==============
let wsConnected=false;
let wsReconnectDelay=1000; // Exponential backoff for WS reconnect
function initWebSocket(){
  const proto=location.protocol==='https:'?'wss':'ws';
  const ws=new WebSocket(proto+'://'+location.hostname+':81/');
  ws.onopen=()=>{wsReconnectDelay=1000;console.log('WS connected, sending auth');ws.send(JSON.stringify({cmd:'auth',token:token}));};
  ws.onclose=()=>{wsConnected=false;console.log('WS closed, reconnect in '+wsReconnectDelay+'ms');setTimeout(initWebSocket,wsReconnectDelay);wsReconnectDelay=Math.min(wsReconnectDelay*2,30000);};
  ws.onerror=()=>{ws.close();};
  ws.onmessage=(e)=>{
    try{
      const d=JSON.parse(e.data);
      if(d.event==='auth-required'){ws.send(JSON.stringify({cmd:'auth',token:token}));return;}
      if(d.event==='auth-ok'){wsConnected=true;console.log('WS auth OK as '+d.user);return;}
      if(d.event==='auth-failed'){console.log('WS auth failed');ws.close();return;}
      if(d.event&&d.event!=='pong'&&d.event!=='connected'){
        // Update storage bar on stats updates
        if(d.event==='stats-update'&&d.sd_free!==undefined){
          const pct=Math.min(100,Math.round((d.sd_used/d.sd_total)*100));
          const bar=document.getElementById('storageBar');
          if(bar){bar.style.width=pct+'%';bar.className='storage-bar-fill '+getStorageColor(pct);}
          const lbl=document.getElementById('storageLabel');
          if(lbl)lbl.textContent=d.sd_free+' KB free';
        }
        // Update SD health panel
        if(d.event==='sd-health'&&d.ok!==undefined){
          const el=document.getElementById('analyticsHealth');
          if(el){
            const freeGB=((d.free_kb||0)/1048576).toFixed(1);
            const totalGB=((d.total_kb||0)/1048576).toFixed(1);
            let html=`<div class="info-row"><span>Status</span><span style="color:${d.ok?'#00b894':'#d63031'}">${d.ok?'✅ Healthy':'❌ Error'}</span></div>`;
            html+=`<div class="info-row"><span>Used</span><span>${d.used_pct||0}% (${freeGB}GB free / ${totalGB}GB total)</span></div>`;
            if(d.write_ops!==undefined)html+=`<div class="info-row"><span>Write Ops</span><span>${(d.write_ops/1000).toFixed(1)}K (${d.write_mb||0}MB written)</span></div>`;
            if(d.risk!==undefined)html+=`<div class="info-row"><span>Failure Risk</span><span style="color:${d.risk>50?'#d63031':d.risk>20?'#fdcb6e':'#00b894'}">${d.risk}%</span></div>`;
            el.innerHTML=html;
          }
        }
        // Auto-refresh on file changes from other sessions
        if(['upload','delete','rename','mkdir','move','copy','restore','empty-trash'].includes(d.event)){
          if(d.path&&d.path.startsWith(currentPath))refreshFiles();
        }
        // Alert on CRC integrity mismatch detected by spot-check
        if(d.event==='crc-mismatch'){
          showToast('⚠️ CRC mismatch: '+d.path,'error');
        }
        // Alert on storage warning (>90% used)
        if(d.event==='storage-warning'){
          showToast('⚠️ Storage '+d.used_pct+'% used! Only '+Math.round((d.free_kb||0)/1024)+'MB free','error');
        }
        // Show upload progress from server-side WebSocket broadcasts
        if(d.event==='upload-progress'&&d.path){
          const el=document.getElementById('ul-'+d.path.replace(/[^a-zA-Z0-9]/g,'_'));
          if(el&&d.total>0){el.textContent=Math.round(d.loaded/d.total*100)+'%';}
        }
      }
    }catch(err){}
  };
}
function switchView(v,btn){
  document.querySelectorAll('.nav-tab').forEach(t=>t.classList.remove('active'));
  document.querySelectorAll('.view').forEach(t=>t.classList.remove('active'));
  if(btn)btn.classList.add('active');else event.target.classList.add('active');
  document.getElementById(v+'View').classList.add('active');
  if(v==='trash')loadTrash();if(v==='users')loadUsers();if(v==='log')loadLog();if(v==='settings')loadSettings();if(v==='apikeys')loadApiKeys();
}
function showToast(msg,type='info'){const t=document.getElementById('toast');t.textContent=msg;t.className='toast '+type+' show';setTimeout(()=>t.classList.remove('show'),3000);}
function openModal(id){document.getElementById(id).style.display='flex';}
function closeModal(id){document.getElementById(id).style.display='none';}
// Confirm dialog: shows a message and calls onConfirm if user agrees
function showConfirm(msg, onConfirm, title='Confirm'){
  document.getElementById('confirmTitle').textContent=title;
  document.getElementById('confirmMsg').textContent=msg;
  const btn=document.getElementById('confirmBtn');
  const newBtn=btn.cloneNode(true);
  btn.parentNode.replaceChild(newBtn,btn);
  newBtn.onclick=()=>{closeModal('confirmModal');onConfirm();};
  openModal('confirmModal');
}
function showShortcuts(){openModal('shortcutsModal');}

// ============== FILES ==============
function downloadZip(){
  // Download current folder as ZIP via /api/folder-zip
  fetch('/api/folder-zip',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'path='+encodeURIComponent(currentPath)+'&csrf='+encodeURIComponent(csrfToken)})
    .then(r=>{if(!r.ok)throw new Error();return r.blob();})
    .then(blob=>{const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='folder.zip';a.click();})
    .catch(()=>showToast('Failed to create ZIP','error'));}
}
function exportFileList(){
  const fmt=prompt("Export format: json or csv?","json");
  if(!fmt||!["json","csv"].includes(fmt))return;
  window.location.href='/api/export-list?format='+fmt+'&path='+encodeURIComponent(currentPath)+'&token='+token;
}
function downloadSelected(){
  if(selectedFiles.length===0)return;
  showToast('Creating ZIP...','info');
  fetch('/api/batch-download',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'plain='+encodeURIComponent(JSON.stringify({paths:selectedFiles}))+'&csrf='+encodeURIComponent(csrfToken)})
    .then(r=>{if(!r.ok)throw new Error();return r.blob();})
    .then(blob=>{const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='selected-files.zip';a.click();})
    .catch(()=>showToast('Failed to create ZIP','error'));}
function loadFiles(path){
  selectedFiles=[];updateSelBtn();hideInfoPanel();
  document.getElementById('fileContainer').innerHTML='<div class="loading-spinner"></div>';
  const sortSelect=document.getElementById('sortSelect');
  const [sortBy,sortDir]=sortSelect.value.split('-');
  fetchRetry('/api/list?path='+encodeURIComponent(path)+'&sort='+sortBy+'&order='+sortDir,{headers:{'Authorization':'Bearer '+token}})
    .then(r=>{if(r.status===401){window.location.href='/login';throw new Error();}return r.json();})
    .then(data=>{
      files=data.files||[];currentPath=path;userLevel=data.userLevel||'user';
      if(userLevel==='admin')document.querySelectorAll('.admin-only').forEach(e=>e.style.display='');
      document.getElementById('userDisplay').textContent='👤 '+data.username;
      renderFiles();renderPathNav(path);
      if(data.storage){
        const pct=Math.round(data.storage.used/data.storage.total*100);
        document.getElementById('storageInfo').innerHTML=`💾 ${formatSize(data.storage.used)} / ${formatSize(data.storage.total)} (${pct}%)<br><span style="font-size:10px">${data.dirCount} folders, ${data.fileCount} files</span><div class="storage-bar"><div class="storage-bar-fill" style="width:${pct}%"></div></div>`;
      }
    })
    .catch(()=>{document.getElementById('fileContainer').innerHTML='<div class="empty-msg">Error loading files</div>';});
}

let typeFilter='all';
function setTypeFilter(type,el){
  typeFilter=type;
  document.querySelectorAll('.filter-chip').forEach(c=>c.classList.remove('active'));
  if(el)el.classList.add('active');
  renderFiles();
}
function renderFiles(){
  let f=[...files];
  const q=document.getElementById('searchInput').value.toLowerCase();
  if(q)f=f.filter(x=>x.name.toLowerCase().includes(q));
  // Apply type filter
  if(typeFilter!=='all'){
    f=f.filter(file=>{
      if(file.type==='dir')return true;
      const ext=(file.name.split('.').pop()||'').toLowerCase();
      switch(typeFilter){
        case'images':return['jpg','jpeg','png','gif','bmp','svg','webp'].includes(ext);
        case'video':return['mp4','avi','mov','mkv','webm'].includes(ext);
        case'audio':return['mp3','wav','ogg','flac','aac'].includes(ext);
        case'docs':return['pdf','doc','docx','txt','md','rtf'].includes(ext);
        case'code':return['c','cpp','h','py','js','html','css','json','xml'].includes(ext);
        case'archives':return['zip','rar','7z','tar','gz'].includes(ext);
        default:return true;
      }
    });
  }
  f.sort((a,b)=>{
    if(a.type==='dir'&&b.type!=='dir')return -1;
    if(a.type!=='dir'&&b.type==='dir')return 1;
    let cmp=0;
    if(sortBy==='name')cmp=a.name.localeCompare(b.name);
    else if(sortBy==='size')cmp=a.size-b.size;
    else if(sortBy==='date')cmp=(a.mtime||0)-(b.mtime||0);
    return sortAsc?cmp:-cmp;
  });
  if(!f.length){document.getElementById('fileContainer').innerHTML='<div class="empty-msg">📂 This folder is empty</div>';return;}
  let html='';
  if(currentPath!=='/'){const parent=currentPath.split('/').slice(0,-2).join('/')+'/';html+=`<div class="file-item" onclick="loadFiles('${parent}')"><div class="file-icon">📁</div><div class="file-name">..</div><div class="file-date"></div><div class="file-size"></div><div class="file-actions"></div></div>`;}
  f.forEach(file=>{
    const sel=selectedFiles.includes(file.path);
    const dateStr=file.mtime?new Date(file.mtime*1000).toLocaleDateString():'';
    html+=`<div class="file-item${sel?' selected':''}" data-path="${file.path}" data-name="${file.name}" data-type="${file.type}" data-readonly="${file.readonly||false}">
      <div class="file-icon">${file.type==='dir'?'📁':file.icon}${file.readonly?'🔒':''}</div>
      <div class="file-name" onclick="${file.type==='dir'?`loadFiles('${file.path}')`:`previewFile('${file.path}')`}">${file.name}</div>
      <div class="file-date" style="text-align:center;font-size:11px;color:var(--text2)">${dateStr}</div>
      <div class="file-size">${file.type==='dir'?'':formatSize(file.size)}</div>
      <div class="file-actions">
        <span class="file-action" onclick="toggleFavorite('${file.path}',event)" title="Favorite" id="fav-${file.path.replace(/[^a-zA-Z0-9]/g,'_')}">☆</span>
        <span class="file-action" onclick="showRenameModal('${file.path}','${file.name}')" title="Rename (F2)">✏️</span>
        ${file.type!=='dir'?`<span class="file-action" onclick="downloadFile('${file.path}')" title="Download">⬇️</span>`:''}
        <span class="file-action" onclick="deleteItem('${file.path}','${file.name}')" title="Delete (Del)">🗑️</span>
      </div>
    </div>`;
  });
  document.getElementById('fileContainer').innerHTML=html;
  document.getElementById('fileCountBadge').textContent=f.length+' item'+(f.length!==1?'s':'')+(selectedFiles.length>0? ' · '+selectedFiles.length+' selected':'');
  document.querySelectorAll('.file-item').forEach(item=>{
    if(!item.dataset.path)return;
    item.addEventListener('contextmenu',e=>{e.preventDefault();showCtxMenu(e,item);});
    // Long-press for mobile context menu
    let longPressTimer = null;
    item.addEventListener('touchstart', e => {
      longPressTimer = setTimeout(() => {
        e.preventDefault();
        const touch = e.touches[0];
        showCtxMenu({clientX: touch.clientX, clientY: touch.clientY, preventDefault: () => {}}, item);
      }, 600);
    }, {passive: false});
    item.addEventListener('touchend', () => { clearTimeout(longPressTimer); });
    item.addEventListener('touchmove', () => { clearTimeout(longPressTimer); });
  });
}
function filterFiles(){renderFiles();}
function setSort(s){if(sortBy===s)sortAsc=!sortAsc;else{sortBy=s;sortAsc=true;}localStorage.setItem('sort_by',sortBy);localStorage.setItem('sort_asc',sortAsc);renderFiles();}
function sortFiles(){const v=document.getElementById('sortSelect').value;const[p,d]=v.split('-');sortBy=p;sortAsc=d==='asc';localStorage.setItem('sort_by',sortBy);localStorage.setItem('sort_asc',sortAsc);loadFiles(currentPath);}
function toggleSel(item){const path=item.dataset.path;if(selectedFiles.includes(path)){selectedFiles=selectedFiles.filter(f=>f!==path);item.classList.remove('selected');}else{selectedFiles.push(path);item.classList.add('selected');}updateSelBtn();}
function updateSelBtn(){const b=document.getElementById('delSelBtn');const c=document.getElementById('copySelBtn');const m=document.getElementById('moveSelBtn');const rn=document.getElementById('renameSelBtn');const d=document.getElementById('downloadSelBtn');const inv=document.getElementById('selectInverseBtn');if(selectedFiles.length>0){b.style.display='';b.textContent='🗑️ Delete ('+selectedFiles.length+')';if(c)c.style.display='';if(m)m.style.display='';if(rn)rn.style.display='';if(d)d.style.display='';if(inv)inv.style.display='';}else{b.style.display='none';if(c)c.style.display='none';if(m)m.style.display='';if(rn)rn.style.display='none';if(d)d.style.display='none';if(inv)inv.style.display='none';}const sa=document.getElementById('selectAllBtn');if(sa)sa.textContent=selectedFiles.length===files.length&&files.length>0?'☐ Deselect All':'☑️ Select All';}
function toggleSelectAll(){
  if(selectedFiles.length===files.length&&files.length>0){selectedFiles=[];updateSelBtn();renderFiles();}
  else{selectedFiles=files.map(f=>f.path);updateSelBtn();renderFiles();}
}
function selectInverse(){
  const allPaths=files.map(f=>f.path);
  selectedFiles=allPaths.filter(p=>!selectedFiles.includes(p));
  updateSelBtn();renderFiles();
}
function renderPathNav(path){const parts=path.split('/').filter(p=>p);const isFav=favorites.includes(path);const starIcon=isFav?'⭐':'☆';const starColor=isFav?'#fdcb6e':'var(--text2)';let html=`<span class="path-part" onclick="loadFiles('/')">Root</span>`,build='/';parts.forEach(p=>{build+=p+'/';html+=`<span class="separator">/</span><span class="path-part" onclick="loadFiles('${build}')">${p}</span>`;});if(path!=='/')html+=`<span style="cursor:pointer;margin-left:6px;font-size:14px;color:${starColor}" onclick="toggleFavorite('${path}',event)" title="Bookmark this folder">${starIcon}</span>`;document.getElementById('pathNav').innerHTML=html;}
function formatSize(b){if(b<1024)return b+' B';if(b<1048576)return(b/1024).toFixed(1)+' KB';if(b<1073741824)return(b/1048576).toFixed(1)+' MB';return(b/1073741824).toFixed(1)+' GB';}

// ============== CONTEXT MENU ==============
function showCtxMenu(e,item){ctxTarget=item;const m=document.getElementById('ctxMenu');m.style.display='block';m.style.left=Math.min(e.clientX,window.innerWidth-200)+'px';m.style.top=Math.min(e.clientY,window.innerHeight-300)+'px';}
function hideCtxMenu(){document.getElementById('ctxMenu').style.display='none';}
function ctxOpen(){if(ctxTarget.dataset.type==='dir')loadFiles(ctxTarget.dataset.path);else previewFile(ctxTarget.dataset.path);hideCtxMenu();}
function ctxPreview(){previewFile(ctxTarget.dataset.path);hideCtxMenu();}
function ctxInfo(){showFileInfo(ctxTarget.dataset.path);hideCtxMenu();}
function ctxCRC(){
  const path=ctxTarget.dataset.path;
  showToast('Computing CRC32...','info');
  fetch('/api/crc?path='+encodeURIComponent(path),{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(d=>{
      if(d.crc32) showToast('CRC32: '+d.crc32+' | Size: '+(d.size||0)+'B'+(d.stored_crc32?' | '+(d.match?'✅ matches stored':'❌ MISMATCH')':''),'success',8000);
      else showToast('CRC computation failed','error');
    }).catch(()=>showToast('Error computing CRC','error'));
  hideCtxMenu();
}
function ctxDownload(){downloadFile(ctxTarget.dataset.path);hideCtxMenu();}
function ctxRename(){showRenameModal(ctxTarget.dataset.path,ctxTarget.dataset.name);hideCtxMenu();}
function ctxMove(){showMoveModal(ctxTarget.dataset.path,'move');hideCtxMenu();}
function ctxCopy(){showMoveModal(ctxTarget.dataset.path,'copy');hideCtxMenu();}
function ctxShare(){shareFile(ctxTarget.dataset.path);hideCtxMenu();}
function ctxCopyPath(){navigator.clipboard.writeText(ctxTarget.dataset.path).then(()=>showToast('Path copied!','success')).catch(()=>showToast('Copy failed','error'));hideCtxMenu();}
function ctxLock(){
  const path=ctxTarget.dataset.path;
  const isLocked=ctxTarget.dataset.readonly==='true';
  const newState=!isLocked;
  fetch('/api/readonly',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'path='+encodeURIComponent(path)+'&readonly='+(newState?'1':'0')+'&csrf='+encodeURIComponent(csrfToken)})
    .then(r=>r.json()).then(d=>{
      if(d.ok){ctxTarget.dataset.readonly=newState?'true':'false';showToast(newState?'🔒 File locked (read-only)':'🔓 File unlocked','success');loadFiles(currentPath);}
      else showToast('Failed to toggle lock','error');
    }).catch(()=>showToast('Lock toggle failed','error'));
  hideCtxMenu();
}
function ctxCompress(){
  const path=ctxTarget.dataset.path;
  const name=ctxTarget.dataset.name||'download';
  showToast('Creating ZIP...','info');
  fetch('/api/zip',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'paths='+encodeURIComponent(JSON.stringify([path]))+'&name='+encodeURIComponent(name)+'&csrf='+encodeURIComponent(csrfToken)})
    .then(r=>{if(r.ok){hideCtxMenu();showToast('ZIP created: '+name+'.zip','success');loadFiles(currentPath);}else showToast('Compress failed (file too large?)','error');})
    .catch(()=>showToast('Compress failed','error'));
}
function ctxDelete(){deleteItem(ctxTarget.dataset.path,ctxTarget.dataset.name);hideCtxMenu();}
function ctxSelectSameType(){
  if(!ctxTarget)return;
  const ext=ctxTarget.dataset.name.split('.').pop().toLowerCase();
  if(!ext){showToast('No extension','error');hideCtxMenu();return;}
  selectedFiles=files.filter(f=>f.name.split('.').pop().toLowerCase()===ext).map(f=>f.path);
  updateSelBtn();renderFiles();
  showToast('Selected '+selectedFiles.length+' .'+ext+' files','info');
  hideCtxMenu();
}

// ============== FILE INFO ==============
function showFileInfo(path){
  fetch('/api/info?path='+encodeURIComponent(path),{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json())
    .then(data=>{
      document.getElementById('infoName').textContent=data.name;
      document.getElementById('infoType').textContent=data.type;
      document.getElementById('infoSize').textContent=data.sizeFormatted;
      document.getElementById('infoPath').textContent=data.path;
      document.getElementById('infoPanel').classList.add('show');
    });
}
function hideInfoPanel(){document.getElementById('infoPanel').classList.remove('show');}

// ============== UPLOAD ==============
function showUploadModal(){document.getElementById('uploadList').innerHTML='';document.getElementById('uploadProgress').style.display='none';document.getElementById('uploadProgressFill').style.width='0%';openModal('uploadModal');}
function showNewFolderModal(){document.getElementById('folderName').value='';openModal('folderModal');document.getElementById('folderName').focus();}
function showNewFileModal(){document.getElementById('newFileName').value='';openModal('newFileModal');document.getElementById('newFileName').focus();}
// ============== RECURSIVE FOLDER UPLOAD ==============
// Uses webkitGetAsEntry + createReader for true recursive folder upload
// Falls back to flat file list if API unsupported
let uploadQueue = [];
let uploadCompleted = 0;
let uploadFailed = 0;
let uploadTotal = 0;
let uploadConcurrency = 3;
let activeUploads = 0;

function handleDrop(e) {
  e.preventDefault();
  e.currentTarget.classList.remove('dragover');
  const items = e.dataTransfer.items;
  if (items && items.length > 0 && items[0].webkitGetAsEntry) {
    // True recursive folder upload
    const entries = [];
    for (let i = 0; i < items.length; i++) {
      const entry = items[i].webkitGetAsEntry();
      if (entry) entries.push(entry);
    }
    collectEntries(entries, '');
  } else {
    handleFiles(e.dataTransfer.files);
  }
}

function collectEntries(entries, prefix) {
  const list = document.getElementById('uploadList');
  list.innerHTML = '';
  uploadQueue = [];
  uploadCompleted = 0;
  uploadFailed = 0;
  const pending = entries.length;
  let resolved = 0;
  entries.forEach(entry => {
    scanEntry(entry, prefix, () => {
      resolved++;
      if (resolved >= pending) {
        startUploadQueue();
      }
    });
  });
  if (entries.length === 0) {
    startUploadQueue();
  }
}

function scanEntry(entry, prefix, done) {
  if (entry.isFile) {
    entry.file(file => {
      const path = prefix + file.name;
      uploadQueue.push({ file, path });
      const id = 'ul-' + path.replace(/[^a-zA-Z0-9]/g, '_');
      const list = document.getElementById('uploadList');
      list.innerHTML += `<div style="display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid var(--border);font-size:12px"><span>${path}</span><span id="${id}">⏳</span></div>`;
      done();
    }, () => { done(); });
  } else if (entry.isDirectory) {
    const dirReader = entry.createReader();
    const subPrefix = prefix + entry.name + '/';
    const readAll = () => {
      dirReader.readEntries(results => {
        if (results.length === 0) {
          done();
          return;
        }
        let subDone = 0;
        results.forEach(child => {
          scanEntry(child, subPrefix, () => {
            subDone++;
            if (subDone >= results.length) {
              readAll(); // Continue reading (some dirs may have >100 entries)
            }
          });
        });
      }, () => { done(); });
    };
    readAll();
  } else {
    done();
  }
}

function handleFiles(fileList){
  const list=document.getElementById('uploadList');list.innerHTML='';
  uploadQueue = [];
  uploadCompleted = 0;
  uploadFailed = 0;
  const MAX_SIZE = 16*1024*1024; // 16 MB — must match MAX_UPLOAD_SIZE in config.h
  let oversized = 0;
  Array.from(fileList).forEach(f=>{
    if(f.size > MAX_SIZE){ oversized++; return; } // Skip files exceeding limit
    const name=f.webkitRelativePath||f.name;
    uploadQueue.push({ file: f, path: name });
    const id='ul-'+name.replace(/[^a-zA-Z0-9]/g,'_');
    list.innerHTML+=`<div style="display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid var(--border);font-size:12px"><span>${name}</span><span id="${id}">⏳</span></div>`;
  });
  if(oversized>0) showToast(oversized+' file(s) exceed 16 MB limit, skipped','error');
  startUploadQueue();
}

function startUploadQueue() {
  uploadTotal = uploadQueue.length;
  uploadCompleted = 0;
  uploadFailed = 0;
  if (uploadTotal === 0) { showToast('Nothing to upload','info'); return; }
  document.getElementById('uploadProgress').style.display='block';
  activeUploads = 0;
  // Start initial batch
  for (let i = 0; i < Math.min(uploadConcurrency, uploadQueue.length); i++) {
    processNextUpload();
  }
}

function processNextUpload() {
  const idx = uploadCompleted + uploadFailed + activeUploads;
  if (idx >= uploadTotal) return; // All dispatched
  if (activeUploads >= uploadConcurrency) return; // At concurrency limit
  const item = uploadQueue[idx];
  activeUploads++;
  uploadSingleFile(item, () => {
    activeUploads--;
    // Update overall progress
    const pct = Math.round((uploadCompleted + uploadFailed) / uploadTotal * 100);
    document.getElementById('uploadProgressFill').style.width = pct + '%';
    if (uploadCompleted + uploadFailed >= uploadTotal) {
      // All done
      setTimeout(() => {
        showToast(`Upload complete: ${uploadCompleted} ok, ${uploadFailed} failed`, uploadFailed === 0 ? 'success' : 'info');
        closeModal('uploadModal');
        loadFiles(currentPath);
      }, 500);
    } else {
      processNextUpload();
    }
  });
}

function uploadSingleFile(item, done) {
  const name = item.path;
  const id = 'ul-' + name.replace(/[^a-zA-Z0-9]/g, '_');
  // Create subdirectories if needed
  const parts = name.split('/');
  let uploadPath = currentPath;
  if (!uploadPath.endsWith('/')) uploadPath += '/';
  if (parts.length > 1) {
    for (let j = 0; j < parts.length - 1; j++) uploadPath += parts[j] + '/';
  }
  fetch('/api/upload-auth', { headers: { 'Authorization': 'Bearer ' + token } }).then(r => r.json()).then(data => {
    const fd = new FormData();
    fd.append('file', item.file);
    fd.append('path', uploadPath);
    fd.append('csrf', csrfToken); // Include CSRF token for upload
    const xhr = new XMLHttpRequest();
    xhr.upload.onprogress = e => {
      if (e.lengthComputable) {
        const pct = Math.round(e.loaded / e.total * 100);
        const el = document.getElementById(id);
        if (el) el.textContent = pct + '%';
      }
    };
    xhr.onload = () => {
      const el = document.getElementById(id);
      if (el) el.textContent = xhr.status === 200 ? '✅' : '❌';
      if (xhr.status === 200) uploadCompleted++;
      else uploadFailed++;
      done();
    };
    xhr.onerror = () => {
      const el = document.getElementById(id);
      if (el) el.textContent = '❌';
      uploadFailed++;
      done();
    };
    xhr.open('POST', '/api/upload?token=' + data.token);
    xhr.send(fd);
  }).catch(() => {
    const el = document.getElementById(id);
    if (el) el.textContent = '❌';
    uploadFailed++;
    done();
  });
}
function createFolder(){
  const name=document.getElementById('folderName').value.trim();
  if(!name){showToast('Enter a folder name','error');return;}
  fetch('/api/create-dir',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'path='+encodeURIComponent(currentPath+name)+'&csrf='+encodeURIComponent(csrfToken)})
    .then(r=>{if(r.status===403){throw new Error('CSRF invalid');}if(!r.ok)throw new Error();return r.text();})
    .then(()=>{showToast('Folder created','success');closeModal('folderModal');loadFiles(currentPath);})
    .catch(e=>showToast(e.message==='CSRF invalid'?'Security token expired, refresh':'Failed','error'));
}
function createFile(){
  const name=document.getElementById('newFileName').value.trim();
  if(!name){showToast('Enter a file name','error');return;}
  fetch('/api/create-file',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'path='+encodeURIComponent(currentPath+name)+'&csrf='+encodeURIComponent(csrfToken)})
    .then(r=>{if(r.status===403){throw new Error('CSRF invalid');}if(!r.ok)throw new Error();return r.text();})
    .then(()=>{showToast('File created','success');closeModal('newFileModal');loadFiles(currentPath);})
    .catch(e=>showToast(e.message==='CSRF invalid'?'Security token expired, refresh':'Failed','error'));
}
function showRenameModal(path,name){document.getElementById('renamePath').value=path;document.getElementById('renameInput').value=name;openModal('renameModal');document.getElementById('renameInput').focus();document.getElementById('renameInput').select();}
function doRename(){
  const path=document.getElementById('renamePath').value;const name=document.getElementById('renameInput').value.trim();
  if(!name){showToast('Enter a name','error');return;}
  fetch('/api/rename',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'path='+encodeURIComponent(path)+'&name='+encodeURIComponent(name)+'&csrf='+encodeURIComponent(csrfToken)})
    .then(r=>{if(r.status===403){throw new Error('CSRF invalid');}if(!r.ok)throw new Error();return r.text();})
    .then(()=>{showToast('Renamed','success');closeModal('renameModal');loadFiles(currentPath);})
    .catch(e=>showToast(e.message==='CSRF invalid'?'Security token expired, refresh':'Failed','error'));
}

// ============== MOVE/COPY ==============
function showMoveModal(path,mode){
  document.getElementById('moveSrcPath').value=path;
  document.getElementById('moveMode').value=mode;
  document.getElementById('moveModalTitle').textContent=mode==='move'?'📦 Move':'📋 Copy';
  // Build folder tree
  buildFolderTree();
  openModal('moveModal');
}

function buildFolderTree(){
  const tree=document.getElementById('folderTree');
  tree.innerHTML='<div class="folder-tree-item selected" data-path="/" onclick="selectFolder(this,\'/\')">📁 / (Root)</div>';
  // Recursively scan directories
  function scanDir(path, depth) {
    if (depth > 4) return; // Limit depth to avoid SD timeout
    fetch('/api/list?path='+encodeURIComponent(path),{headers:{'Authorization':'Bearer '+token}})
      .then(r=>r.json()).then(data=>{
        (data.files||[]).filter(f=>f.type==='dir').forEach(f=>{
          const indent='&nbsp;&nbsp;'.repeat(depth);
          tree.innerHTML+=`<div class="folder-tree-item" data-path="${f.path}" onclick="selectFolder(this,'${f.path}')">${indent}📁 ${f.name}</div>`;
          scanDir(f.path, depth+1);
        });
      }).catch(()=>{});
  }
  scanDir(currentPath, 1);
}

function selectFolder(el,path){
  document.querySelectorAll('.folder-tree-item').forEach(i=>i.classList.remove('selected'));
  el.classList.add('selected');
  document.getElementById('moveDestPath').value=path;
}

function doMoveCopy(){
  const src=document.getElementById('moveSrcPath').value;
  const mode=document.getElementById('moveMode').value;
  const sel=document.querySelector('.folder-tree-item.selected');
  const dest=sel?sel.dataset.path:'/';
  const endpoint=mode==='move'?'/api/move':'/api/copy';
  fetch(endpoint,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'path='+encodeURIComponent(src)+'&dest='+encodeURIComponent(dest)+'&csrf='+encodeURIComponent(csrfToken)})
    .then(r=>{if(r.status===403){throw new Error('CSRF invalid');}if(!r.ok)throw new Error();return r.text();})
    .then(()=>{showToast(mode==='move'?'Moved':'Copied','success');closeModal('moveModal');loadFiles(currentPath);})
    .catch(e=>showToast(e.message==='CSRF invalid'?'Security token expired, refresh':'Failed to '+mode,'error'));}
}

function previewFile(path){
  const ext=path.split('.').pop().toLowerCase();
  const previewable=['jpg','jpeg','png','gif','bmp','svg','webp','txt','html','htm','css','js','json','xml','md','csv','pdf','mp3','wav','ogg','mp4','mov','avi'];
  if(!previewable.includes(ext)){downloadFile(path);return;}
  const type=['jpg','jpeg','png','gif','bmp','svg','webp'].includes(ext)?'image':['mp3','wav','ogg'].includes(ext)?'audio':['mp4','mov','avi'].includes(ext)?'video':ext==='pdf'?'pdf':'text';
  const content=document.getElementById('previewContent');
  document.getElementById('previewTitle').textContent=path.split('/').pop();
  if(type==='image'){content.innerHTML=`<img src="${path}?token=${token}" alt="preview">`;}
  else if(type==='audio'){content.innerHTML=`<audio controls src="${path}?token=${token}"></audio>`;}
  else if(type==='video'){content.innerHTML=`<video controls preload="metadata" poster="/api/video?path=${encodeURIComponent(path)}&thumb=1&token=${token}" src="/api/video?path=${encodeURIComponent(path)}&token=${token}"></video>`;}
  else if(type==='pdf'){content.innerHTML=`<iframe src="/api/download?path=${encodeURIComponent(path)}&token=${token}" style="width:100%;height:60vh;border:none;border-radius:6px"></iframe>`;}
  else{
    // Use markdown preview for .md files, /api/preview for other text
    if(ext==='md'){
      fetch('/api/md-preview?path='+encodeURIComponent(path)+'&token='+token).then(r=>r.text()).then(html=>{
        content.innerHTML=`<div style="padding:10px;max-height:60vh;overflow:auto">${html}</div>`;
      }).catch(()=>{content.innerHTML='<p>Cannot preview</p>';});
    } else {
      // Use /api/preview for sanitized text content
      fetch('/api/preview?path='+encodeURIComponent(path)+'&token='+token).then(r=>r.json()).then(d=>{
        if(d.error){content.innerHTML=`<p style="color:var(--danger)">${d.error}</p>`;}
        else{
          const escaped=d.content.replace(/</g,'&lt;').replace(/>/g,'&gt;');
          content.innerHTML=`<pre style="white-space:pre-wrap;word-break:break-all;font-family:monospace;font-size:13px;max-height:60vh;overflow:auto">${escaped}</pre>`;
          if(d.truncated){content.innerHTML+=`<p style="color:var(--text2);font-size:12px;margin-top:8px">⚠️ Preview truncated (showing first 64KB)</p>`;}
        }
      }).catch(()=>{content.innerHTML='<p>Cannot preview</p>';});
    }
  }
  openModal('previewModal');
}
// ============== RECENT FILES ==============
function showRecentFiles(){
  fetch('/api/recent?limit=30&token='+token).then(r=>r.json()).then(d=>{
    if(!d.recent||d.recent.length===0){showToast('No recent files found','info');return;}
    const list=d.recent.map(f=>`<div class="file-item" onclick="openRecent('${f.path}')" style="cursor:pointer;display:flex;align-items:center;gap:10px;padding:8px 12px;border-bottom:1px solid var(--border)">
      <span style="font-size:18px">${f.icon}</span>
      <div style="flex:1;min-width:0"><div style="font-size:13px;font-weight:500;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">${f.name}</div>
      <div style="font-size:11px;color:var(--text2)">${f.sizeFormatted} · ${f.path}</div></div></div>`).join('');
    document.getElementById('recentTitle').textContent='🕐 Recent Files';
    document.getElementById('recentList').innerHTML=list;
    openModal('recentModal');
  }).catch(()=>showToast('Failed to load recent files','error'));
}
function openRecent(path){
  closeModal('recentModal');
  const dir=path.substring(0,path.lastIndexOf('/'))||'/';
  loadFiles(dir);
}
function downloadFile(path){window.location.href='/api/download?path='+encodeURIComponent(path)+'&token='+token;}
// ============== FAVORITES ==============
let favorites = JSON.parse(localStorage.getItem('favorites') || '[]');
function toggleFavorite(path, e){
  e.stopPropagation();
  const idx = favorites.indexOf(path);
  if(idx >= 0){ favorites.splice(idx,1); }
  else{ favorites.push(path); }
  localStorage.setItem('favorites', JSON.stringify(favorites));
  // Update star icon
  const el = document.getElementById('fav-' + path.replace(/[^a-zA-Z0-9]/g,'_'));
  if(el) el.textContent = idx >= 0 ? '☆' : '⭐';
  // Sync to server
  fetch('/api/favorites/toggle',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'path='+encodeURIComponent(path)+'&csrf='+encodeURIComponent(csrfToken)}).catch(()=>{});
}
function initFavorites(){
  favorites.forEach(p=>{
    const el = document.getElementById('fav-' + p.replace(/[^a-zA-Z0-9]/g,'_'));
    if(el) el.textContent = '⭐';
  });
}
function shareFile(path){
  fetch('/api/share',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'path='+encodeURIComponent(path)+'&csrf='+encodeURIComponent(csrfToken)})
    .then(r=>r.json()).then(data=>{document.getElementById('shareUrl').value=data.url;document.getElementById('shareExpiry').textContent=data.expires||'24h';openModal('shareModal');})
    .catch(()=>showToast('Failed to create share link','error'));}
}
function copyShareUrl(){const input=document.getElementById('shareUrl');input.select();navigator.clipboard.writeText(input.value);showToast('Link copied!','success');}
function shareViaEmail(){
  const url=document.getElementById('shareUrl').value;
  const subject='Shared file from ESP32';
  const body='Download the file here: '+url;
  window.open('mailto:?subject='+encodeURIComponent(subject)+'&body='+encodeURIComponent(body));
}
function deleteItem(path,name){
  showConfirm(`Move "${name}" to trash?`, ()=>{
    fetch('/api/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'path='+encodeURIComponent(path)+'&csrf='+encodeURIComponent(csrfToken)})
      .then(r=>{if(!r.ok)throw new Error();return r.text();})
      .then(()=>{showToast('Moved to trash','success');loadFiles(currentPath);})
      .catch(()=>showToast('Failed','error'));
  },'Delete');
}
function deleteSelected(){
  if(!selectedFiles.length)return;
  showConfirm(`Move ${selectedFiles.length} items to trash?`, ()=>{
    fetch('/api/batch-delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'plain='+encodeURIComponent(JSON.stringify({paths:selectedFiles}))+'&csrf='+encodeURIComponent(csrfToken)})
      .then(r=>{if(!r.ok)throw new Error();return r.text();})
      .then(()=>{selectedFiles=[];updateSelBtn();showToast('Moved to trash','success');loadFiles(currentPath);})
      .catch(()=>{showToast('Some failed','error');loadFiles(currentPath);});
  },'Batch Delete');
}
}
function copySelected(){
  if(!selectedFiles.length)return;
  // Prompt user for destination
  const dest=prompt('Copy to path (e.g. /backup/):',currentPath);
  if(!dest)return;
  fetch('/api/batch-copy',{method:'POST',headers:{'Authorization':'Bearer '+token,'Content-Type':'application/json','X-CSRF-Token':csrfToken},body:JSON.stringify({paths:selectedFiles,dest:dest})})
    .then(r=>r.json()).then(data=>{
      if(data.ok>0)showToast('Copied '+data.ok+' item(s)','success');
      if(data.fail>0)showToast(data.fail+' failed','error');
      loadFiles(currentPath);
    })
    .catch(()=>showToast('Copy failed','error'));
}
function moveSelected(){
  if(!selectedFiles.length)return;
  const dest=prompt('Move to path (e.g. /backup/):',currentPath);
  if(!dest)return;
  fetch('/api/batch-move',{method:'POST',headers:{'Authorization':'Bearer '+token,'Content-Type':'application/json','X-CSRF-Token':csrfToken},body:JSON.stringify({paths:selectedFiles,dest:dest})})
    .then(r=>r.json()).then(data=>{
      if(data.ok>0)showToast('Moved '+data.ok+' item(s)','success');
      if(data.fail>0)showToast(data.fail+' failed','error');
      loadFiles(currentPath);
    })
    .catch(()=>showToast('Move failed','error'));
}
// ============== BATCH RENAME ==============
function renameSelected(){
  if(!selectedFiles.length)return;
  const find=prompt('Find in filename:');
  if(!find)return;
  const replace=prompt('Replace with:','');
  fetch('/api/batch-rename',{method:'POST',headers:{'Authorization':'Bearer '+token,'Content-Type':'application/json','X-CSRF-Token':csrfToken},body:JSON.stringify({paths:selectedFiles,find:find,replace:replace})})
    .then(r=>r.json()).then(data=>{
      if(data.ok>0)showToast('Renamed '+data.ok+' item(s)','success');
      if(data.fail>0)showToast(data.fail+' failed','error');
      loadFiles(currentPath);
    })
    .catch(()=>showToast('Rename failed','error'));
}
function refreshFiles(){loadFiles(currentPath);}

// ============== TRASH ==============
let trashSelectedFiles = [];
function loadTrash(){
  fetch('/api/trash',{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(data=>{
      trashFiles=data.files||[];
      trashSelectedFiles=[];
      if(!trashFiles.length){document.getElementById('trashContainer').innerHTML='<div class="empty-msg">🗑️ Trash is empty</div>';return;}
      let html='';
      trashFiles.forEach(f=>{
        const sel=trashSelectedFiles.includes(f.path);
        html+=`<div class="file-item${sel?' selected':''}" data-path="${f.path}">
          <div class="file-icon">${f.type==='dir'?'📁':'📄'}</div>
          <div class="file-name">${f.name}</div>
          <div class="file-size">${f.type==='dir'?'':formatSize(f.size)}</div>
          <div class="file-actions" style="justify-content:flex-end">
            <span class="file-action" onclick="restoreItem('${f.path}')">♻️</span>
            <span class="file-action" onclick="toggleTrashSelect(this.closest('.file-item'))" title="Select">☑️</span>
            <span class="file-action" onclick="permanentDelete('${f.path}','${f.name}')">❌</span>
          </div>
        </div>`;
      });
      document.getElementById('trashContainer').innerHTML=html;
      updateRestoreBtn();
    })
    .catch(()=>{document.getElementById('trashContainer').innerHTML='<div class="empty-msg">Error loading trash</div>';});
}
function toggleTrashSelect(el){
  const path=el.dataset.path;
  if(trashSelectedFiles.includes(path)){trashSelectedFiles=trashSelectedFiles.filter(f=>f!==path);el.classList.remove('selected');}
  else{trashSelectedFiles.push(path);el.classList.add('selected');}
  updateRestoreBtn();
}
function updateRestoreBtn(){
  const b=document.getElementById('restoreSelBtn');
  if(b)b.style.display=trashSelectedFiles.length>0?'':'none';
}
function restoreSelected(){
  if(!trashSelectedFiles.length)return;
  fetch('/api/batch-restore',{method:'POST',headers:{'Content-Type':'application/json','Authorization':'Bearer '+token,'X-CSRF-Token':csrfToken},body:JSON.stringify({paths:trashSelectedFiles})})
    .then(r=>r.json()).then(d=>{
      if(d.ok>0)showToast('Restored '+d.ok+' item(s)','success');
      if(d.fail>0)showToast(d.fail+' failed','error');
      loadTrash();
    })
    .catch(()=>showToast('Restore failed','error'));
}
function restoreItem(path){
  fetch('/api/restore?path='+encodeURIComponent(path),{headers:{'Authorization':'Bearer '+token}})
    .then(r=>{if(r.ok){showToast('Restored','success');loadTrash();}else showToast('Failed','error');})
    .catch(()=>showToast('Failed','error'));
}
function permanentDelete(path,name){
  document.getElementById('confirmMsg').textContent=`Permanently delete "${name}"?`;
  document.getElementById('confirmBtn').onclick=()=>{
    fetch('/api/empty-trash',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token,'X-CSRF-Token':csrfToken},body:'path='+encodeURIComponent(path)})
      .then(r=>{if(r.ok){showToast('Deleted','success');loadTrash();}else showToast('Failed','error');closeModal('confirmModal');})
      .catch(()=>{showToast('Failed','error');closeModal('confirmModal');});
  };
  openModal('confirmModal');
}
function emptyTrash(){
  document.getElementById('confirmMsg').textContent='Empty all trash? This cannot be undone.';
  document.getElementById('confirmBtn').onclick=()=>{
    fetch('/api/empty-trash',{method:'POST',headers:{'Authorization':'Bearer '+token,'X-CSRF-Token':csrfToken}})
      .then(r=>{if(r.ok){showToast('Trash emptied','success');loadTrash();}else showToast('Failed','error');closeModal('confirmModal');})
      .catch(()=>{showToast('Failed','error');closeModal('confirmModal');});
  };
  openModal('confirmModal');
}

// ============== USERS ==============
function loadUsers(){
  fetch('/api/users',{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(data=>{
      users=data.users||[];
      let html='';
      users.forEach((u,i)=>{html+=`<tr><td style="padding:10px 15px;font-size:13px">${u.username}</td><td style="padding:10px 15px;font-size:13px">${u.userLevel}</td><td style="padding:10px 15px;text-align:right"><button class="btn btn-sm" onclick="editUser(${i})">✏️</button> <button class="btn btn-sm btn-danger" onclick="deleteUser(${i})">🗑️</button></td></tr>`;});
      document.getElementById('userTable').innerHTML=html;
    });
}
function showUserModal(){document.getElementById('userModalTitle').textContent='Add User';document.getElementById('uName').value='';document.getElementById('uPass').value='';document.getElementById('uRole').value='user';document.getElementById('uName').disabled=false;openModal('userModal');document.getElementById('uName').focus();}
function editUser(i){document.getElementById('userModalTitle').textContent='Edit User';document.getElementById('uName').value=users[i].username;document.getElementById('uPass').value='';document.getElementById('uRole').value=users[i].userLevel;document.getElementById('uName').disabled=true;openModal('userModal');}
function saveUser(){
  const n=document.getElementById('uName').value,p=document.getElementById('uPass').value,r=document.getElementById('uRole').value;
  const isEdit=document.getElementById('uName').disabled;
  const body={username:n,userLevel:r};if(p)body.password=p;
  fetch(isEdit?'/api/users/'+n:'/api/users',{method:isEdit?'PUT':'POST',headers:{'Content-Type':'application/json','Authorization':'Bearer '+token},body:JSON.stringify(body)})
    .then(r=>{if(r.ok){closeModal('userModal');loadUsers();showToast('Saved','success');}else showToast('Failed','error');});
}
function deleteUser(i){if(!confirm('Delete user "'+users[i].username+'"?'))return;fetch('/api/users/'+users[i].username,{method:'DELETE',headers:{'Authorization':'Bearer '+token}}).then(r=>{if(r.ok){loadUsers();showToast('Deleted','success');}else showToast('Failed','error');});}

// ============== SETTINGS ==============
function loadSettings(){
  fetch('/api/settings',{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(data=>{
      document.getElementById('setWifiSsid').value=data.saved_wifi_ssid||data.wifi_ssid||'';
      document.getElementById('setApSsid').value=data.saved_ap_ssid||data.ap_ssid||'';
      document.getElementById('setFtpUser').value=data.ftp_user||'';
      document.getElementById('setWebPort').value=data.web_port||80;
      document.getElementById('sysVersion').textContent=data.version||'';
      document.getElementById('sysIp').textContent=data.ip||'';
      document.getElementById('sysMode').textContent=data.mode||'';
      document.getElementById('sysUptime').textContent=formatUptime(data.uptime||0);
      document.getElementById('sysHeap').textContent=formatSize(data.free_heap||0);
    });
  // Load quota list
  fetch('/api/quota',{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(data=>{
      if(!data.length){document.getElementById('quotaList').textContent='No quotas set (unlimited)';return;}
      let html='';data.forEach(q=>{html+=`<div style="margin:4px 0"><strong>${q.user}</strong>: ${q.limit_mb||0} MB${q.limit===0?' (unlimited)':''}</div>`;});
      document.getElementById('quotaList').innerHTML=html;
    }).catch(()=>{document.getElementById('quotaList').textContent='Failed to load quotas';});
}
function setQuota(){
  const user=document.getElementById('quotaUser').value.trim();
  const limit=parseInt(document.getElementById('quotaLimit').value)||0;
  if(!user){showToast('Enter a username','error');return;}
  fetch('/api/quota',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token,'X-CSRF-Token':csrfToken},body:'user='+encodeURIComponent(user)+'&limit='+limit})
    .then(r=>r.json()).then(data=>{if(data.ok){showToast('Quota set for '+user,'success');loadSettings();}else showToast('Failed','error');})
    .catch(()=>showToast('Failed to set quota','error'));
}
function formatUptime(s){const h=Math.floor(s/3600),m=Math.floor((s%3600)/60);return h+'h '+m+'m';}
function loadApiKeys(){
  fetch('/api/api-keys',{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(data=>{
      const list=data.api_keys||[];
      if(!list.length){document.getElementById('apiKeyList').innerHTML='<p style="color:var(--text2)">No API keys yet</p>';return;}
      let html='<div style="display:flex;flex-direction:column;gap:6px">';
      list.forEach(k=>{
        html+=`<div style="display:flex;align-items:center;gap:8px;background:var(--bg);padding:8px;border-radius:6px">
          <code style="flex:1;font-size:12px;overflow:hidden;text-overflow:ellipsis">${k.key}</code>
          <span style="font-size:12px;color:var(--text2)">${k.username} (${k.level})</span>
          <button class="btn btn-sm btn-danger" onclick="revokeApiKey('${k.key}')">Revoke</button>
        </div>`;
      });
      html+='</div>';
      document.getElementById('apiKeyList').innerHTML=html;
    }).catch(()=>{document.getElementById('apiKeyList').innerHTML='<p style="color:var(--danger)">Failed to load</p>';});
}
function createApiKey(){
  if(!confirm('Generate a new API key? It will only be shown once.'))return;
  fetch('/api/api-keys',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'csrf='+encodeURIComponent(csrfToken)})
    .then(r=>r.json()).then(data=>{
      if(data.api_key){prompt('Copy your API key (shown only once):',data.api_key);loadApiKeys();}
      else showToast(data.error||'Failed','error');
    }).catch(()=>showToast('Failed to create key','error'));
}
function revokeApiKey(key){
  if(!confirm('Revoke this key? Applications using it will lose access.'))return;
  fetch('/api/api-keys',{method:'DELETE',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'key='+encodeURIComponent(key)+'&csrf='+encodeURIComponent(csrfToken)})
    .then(r=>r.json()).then(data=>{if(data.ok){showToast('Key revoked','success');loadApiKeys();}else showToast('Failed','error');})
    .catch(()=>showToast('Failed to revoke','error'));
}
function saveSettings(){
  const body={
    wifi_ssid:document.getElementById('setWifiSsid').value,
    wifi_pass:document.getElementById('setWifiPass').value,
    ap_ssid:document.getElementById('setApSsid').value,
    ap_pass:document.getElementById('setApPass').value,
    ftp_user:document.getElementById('setFtpUser').value,
    ftp_pass:document.getElementById('setFtpPass').value,
    web_port:parseInt(document.getElementById('setWebPort').value)||80
  };
  fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json','Authorization':'Bearer '+token,'X-CSRF-Token':csrfToken},body:JSON.stringify(body)})
    .then(r=>r.json()).then(data=>{
      if(data.reboot){showToast(data.msg,'info');setTimeout(()=>location.reload(),3000);}
      else showToast(data.msg||'Settings saved','success');
    })
    .catch(()=>showToast('Failed to save','error'));
}

// ============== ACTIVITY LOG ==============
function loadLog(){
  fetch('/api/log',{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(data=>{
      const entries=data.log||[];
      if(!entries.length){document.getElementById('logTable').innerHTML='<tr><td colspan="4" style="padding:20px;text-align:center;color:var(--text2)">No activity recorded</td></tr>';return;}
      let html='';
      entries.reverse().forEach(e=>{
        const cls='log-'+(e.action||'').split('-')[0];
        const time=e.time?new Date(e.time).toLocaleString():'';
        html+=`<tr><td style="white-space:nowrap">${time}</td><td>${e.user||''}</td><td><span class="log-action ${cls}">${e.action||''}</span></td><td style="word-break:break-all">${e.path||''}</td></tr>`;
      });
      document.getElementById('logTable').innerHTML=html;
    })
    .catch(()=>{document.getElementById('logTable').innerHTML='<tr><td colspan="4" style="padding:20px;text-align:center;color:var(--danger)">Error</td></tr>';});
}

// ============== STORAGE ANALYTICS ==============
function loadAnalytics(){
  // Load storage breakdown
  fetch('/api/analytics',{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(data=>{
      const b=data.breakdown||{};
      let html='';
      const types=[
        {key:'images',icon:'🖼️',label:'Images'},
        {key:'video',icon:'🎬',label:'Video'},
        {key:'audio',icon:'🎵',label:'Audio'},
        {key:'documents',icon:'📄',label:'Documents'},
        {key:'archives',icon:'📦',label:'Archives'},
        {key:'code',icon:'💻',label:'Code'},
        {key:'other',icon:'📎',label:'Other'}
      ];
      types.forEach(t=>{
        const c=b[t.key]||{count:0,size:0,size_fmt:'0 B'};
        html+=`<div class="info-row"><span>${t.icon} ${t.label}</span><span>${c.count} files · ${c.size_fmt||c.size}</span></div>`;
      });
      html+=`<div class="info-row" style="font-weight:600;border-top:2px solid var(--border);margin-top:4px;padding-top:8px"><span>Total</span><span>${data.total_files||0} files · ${data.total_size_formatted||'0 B'}</span></div>`;
      document.getElementById('analyticsBreakdown').innerHTML=html;
    }).catch(()=>{document.getElementById('analyticsBreakdown').innerHTML='<p style="color:var(--danger)">Error loading</p>';});
  // Load SD health
  fetch('/api/sd-health',{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(data=>{
      const totalGB=((data.total||0)/1024).toFixed(1);
      const freeGB=((data.free||0)/1024).toFixed(1);
      const usedPct=totalGB>0?((totalGB-freeGB)/totalGB*100).toFixed(0):0;
      let html=`<div class="info-row"><span>Status</span><span style="color:${data.ok?'#00b894':'#d63031'}">${data.ok?'✅ Healthy':'❌ Error'}</span></div>`;
      html+=`<div class="info-row"><span>Used</span><span>${usedPct}% (${freeGB}GB free / ${totalGB}GB total)</span></div>`;
      if(data.total_write_ops!==undefined) html+=`<div class="info-row"><span>Write Ops</span><span>${(data.total_write_ops/1000).toFixed(1)}K (${(data.total_write_mb||0)}MB written)</span></div>`;
      if(data.card_type!==undefined){const types={0:'SD',1:'SDHC',2:'SDXC',3:'MMC'};html+=`<div class="info-row"><span>Card Type</span><span>${types[data.card_type]||'Unknown'}</span></div>`;}
      document.getElementById('analyticsHealth').innerHTML=html;
      const wear=data.wear_percent||0;
      const wf=document.getElementById('wearFill');
      if(wf){wf.style.width=wear+'%';wf.style.background=wear>50?'#d63031':wear>20?'#fdcb6e':'#00b894';}
    }).catch(()=>{document.getElementById('analyticsHealth').innerHTML='<p style="color:var(--danger)">Error loading</p>';});
  // Load space usage breakdown
  fetch('/api/space-usage?path=/'+encodeURIComponent(currentPath),{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(data=>{
      if(data.breakdown && data.breakdown.length>0){
        let html='<div class="info-row" style="font-weight:600;margin-bottom:6px"><span>Disk Usage by Item</span><span>Size</span></div>';
        data.breakdown.slice(0,10).forEach(e=>{
          const icon=e.is_dir?'📁':'📄';
          html+=`<div class="info-row"><span>${icon} ${e.name}</span><span>${e.size_fmt}</span></div>`;
        });
        const el=document.getElementById('dupResults');
        if(el) el.innerHTML=html;
      }
    }).catch(()=>{});
  // Load file type distribution bars
  fetch('/api/list',{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(data=>{
      const files=data.files||[];
      const exts={};
      files.forEach(f=>{
        if(f.type==='dir')return;
        const e=(f.name.split('.').pop()||'').toLowerCase()||'no-ext';
        exts[e]=(exts[e]||0)+1;
      });
      const sorted=Object.entries(exts).sort((a,b)=>b[1]-a[1]).slice(0,10);
      const max=sorted.length>0?sorted[0][1]:1;
      let html='<div style="display:flex;flex-direction:column;gap:4px;margin-top:8px">';
      sorted.forEach(([ext,cnt])=>{
        const pct=(cnt/max*100).toFixed(0);
        html+=`<div style="display:flex;align-items:center;gap:8px;font-size:12px"><span style="width:55px;text-align:right;font-family:monospace">${ext}</span><div style="flex:1;height:18px;background:var(--bg);border-radius:3px;overflow:hidden"><div style="height:100%;width:${pct}%;background:var(--primary);display:flex;align-items:center;padding-left:4px;color:#fff;font-size:11px">${cnt}</div></div></div>`;
      });
      html+='</div>';
      document.getElementById('typeBars').innerHTML=html;
    }).catch(()=>{document.getElementById('typeBars').innerHTML='<p style="color:var(--danger)">Error</p>';});
  // Load duplicates
  fetch('/api/duplicates',{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(data=>{
      if(!data.duplicates||data.duplicates.length===0){
        document.getElementById('dupResults').innerHTML=`<p style="color:var(--text2)">✅ No duplicates found (scanned ${data.files_scanned||0} files)</p>`;
        return;
      }
      let html=`<p style="margin-bottom:8px;color:var(--text2)">Found <strong>${data.groups}</strong> duplicate groups (scanned ${data.files_scanned||0} files)</p>`;
      data.duplicates.forEach((g,i)=>{
        html+=`<div style="background:var(--bg);border-radius:6px;padding:8px;margin-bottom:6px">`;
        html+=`<div style="font-size:12px;color:var(--text2);margin-bottom:4px">📦 ${g.size_formatted||g.size+'B'} · CRC: ${g.crc32?.substring(0,8)||''} · ${g.files.length} copies</div>`;
        g.files.forEach(p=>{html+=`<div style="font-size:11px;padding:2px 0;font-family:monospace;color:var(--text2)">${p}</div>`;});
        html+=`</div>`;
      });
      document.getElementById('dupResults').innerHTML=html;
    }).catch(()=>{document.getElementById('dupResults').innerHTML='<p style="color:var(--danger)">Error scanning</p>';});
  // Load audit log
  fetch('/api/audit?limit=20',{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json()).then(data=>{
      if(!data.entries||data.entries.length===0){
        document.getElementById('auditLog').innerHTML='<p style="color:var(--text2)">No audit entries</p>';
        return;
      }
      let html='<table style="width:100%;font-size:11px;border-collapse:collapse"><tr style="color:var(--text2);border-bottom:1px solid var(--border)"><th style="text-align:left;padding:4px">Time</th><th style="text-align:left;padding:4px">IP</th><th style="text-align:left;padding:4px">Action</th><th style="text-align:left;padding:4px">Detail</th></tr>';
      data.entries.forEach(e=>{
        const acted=e.action||'';
        const color=acted.includes('fail')?'var(--danger)':acted.includes('ok')?'var(--success)':'var(--text)';
        html+=`<tr style="color:${color};border-bottom:1px solid var(--border)"><td style="padding:3px">${Math.round(e.time/1000)}s</td><td style="padding:3px;font-family:monospace">${e.ip||''}</td><td style="padding:3px">${acted}</td><td style="padding:3px">${e.detail||''}</td></tr>`;
      });
      html+='</table>';
      document.getElementById('auditLog').innerHTML=html;
    }).catch(()=>{document.getElementById('auditLog').innerHTML='<p style="color:var(--text2)">Audit log unavailable</p>';});
}
// ============== PWA SERVICE WORKER REGISTRATION ==============
if('serviceWorker' in navigator){navigator.serviceWorker.register('/sw.js').then(()=>console.log('SW registered')).catch(()=>console.log('SW skipped'));}
</script>
</body>
</html>
)rawliteral";

const char login_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>ESP32 File Server - Login</title>
<style>
:root{--primary:#0984e3;--bg:#f5f6fa;--card:#fff;--text:#2d3436;--danger:#d63031}
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',system-ui,sans-serif}
body{background:var(--bg);display:flex;justify-content:center;align-items:center;min-height:100vh;padding:20px}
.login-box{width:100%;max-width:380px;padding:35px;background:var(--card);border-radius:12px;box-shadow:0 4px 20px rgba(0,0,0,0.1)}
h1{text-align:center;color:var(--primary);font-size:24px;margin-bottom:5px}
.sub{text-align:center;color:#999;font-size:13px;margin-bottom:25px}
.form-group{margin-bottom:15px}
label{display:block;margin-bottom:5px;font-weight:500;font-size:13px;color:var(--text)}
input[type="text"],input[type="password"]{width:100%;padding:12px;border:1px solid #ddd;border-radius:8px;font-size:15px;transition:border .2s;min-height:44px}
input:focus{outline:none;border-color:var(--primary)}
.btn{width:100%;background:var(--primary);color:#fff;border:none;padding:12px;border-radius:8px;cursor:pointer;font-size:15px;font-weight:600;margin-top:5px;transition:background .2s;min-height:44px}
.btn:hover{background:#0652DD}
.error{color:var(--danger);font-size:13px;text-align:center;margin-top:15px}
.info{text-align:center;font-size:11px;color:#999;margin-top:25px}
</style>
</head>
<body>
<div class="login-box">
  <h1>📁 ESP32 File Server</h1>
  <p class="sub">Please log in to continue</p>
  <form method="post" action="/login">
    <input type="hidden" name="csrf" value="%CSRF%">
    <div class="form-group"><label>Username</label><input type="text" name="username" required autofocus></div>
    <div class="form-group"><label>Password</label><input type="password" name="password" required></div>
    <button type="submit" class="btn">Log In</button>
  </form>
  <div class="error">%ERROR%</div>
  <div class="info">%INFO%</div>
</div>
</body>
</html>
)rawliteral";

#endif
