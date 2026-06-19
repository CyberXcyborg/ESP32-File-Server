#ifndef WEB_UI_H
#define WEB_UI_H

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 File Server v2</title>
<style>
:root{--bg:#f5f6fa;--card:#fff;--text:#2d3436;--text2:#636e72;--primary:#0984e3;--primary-dark:#0652DD;--danger:#d63031;--success:#00b894;--border:#dfe6e9;--shadow:0 2px 10px rgba(0,0,0,0.08);--radius:8px}
.dark{--bg:#1a1a2e;--card:#16213e;--text:#e0e0e0;--text2:#a0a0a0;--primary:#74b9ff;--primary-dark:#0984e3;--danger:#ff7675;--success:#55efc4;--border:#2d3748;--shadow:0 2px 10px rgba(0,0,0,0.3)}
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',system-ui,sans-serif}
body{background:var(--bg);color:var(--text);transition:background .3s,color .3s;line-height:1.5}
.container{max-width:1200px;margin:0 auto;padding:15px}
header{display:flex;justify-content:space-between;align-items:center;padding:15px 20px;background:var(--card);border-radius:var(--radius);box-shadow:var(--shadow);margin-bottom:15px;flex-wrap:wrap;gap:10px}
header h1{font-size:20px;color:var(--primary)}
.header-right{display:flex;align-items:center;gap:12px;font-size:13px;color:var(--text2)}
.header-right span{color:var(--primary);font-weight:600}
.btn{background:var(--primary);color:#fff;border:none;padding:8px 14px;border-radius:6px;cursor:pointer;font-size:13px;display:inline-flex;align-items:center;gap:5px;transition:all .2s;text-decoration:none}
.btn:hover{background:var(--primary-dark);transform:translateY(-1px)}
.btn-danger{background:var(--danger)}.btn-danger:hover{background:#c0392b}
.btn-ghost{background:transparent;color:var(--text);border:1px solid var(--border)}
.btn-ghost:hover{background:var(--border)}
.btn-sm{padding:5px 10px;font-size:12px}
.search-box{display:flex;align-items:center;background:var(--bg);border:1px solid var(--border);border-radius:6px;padding:0 10px;gap:8px}
.search-box input{border:none;background:transparent;padding:8px 0;outline:none;color:var(--text);font-size:13px;width:180px}
.search-box input::placeholder{color:var(--text2)}
.controls{display:flex;gap:8px;margin-bottom:12px;flex-wrap:wrap;align-items:center}
.path-nav{display:flex;align-items:center;padding:10px 15px;background:var(--card);border-radius:var(--radius);margin-bottom:12px;overflow-x:auto;white-space:nowrap;font-size:13px;box-shadow:var(--shadow)}
.path-part{cursor:pointer;color:var(--primary);margin-right:4px}
.path-part:hover{text-decoration:underline}
.separator{margin:0 4px;color:var(--text2)}
.file-list{background:var(--card);border-radius:var(--radius);box-shadow:var(--shadow);overflow:hidden}
.file-list-header{display:grid;grid-template-columns:30px 1fr 100px 80px 100px;background:var(--primary);color:#fff;padding:10px 15px;font-weight:600;font-size:13px;cursor:pointer;user-select:none}
.file-list-header div:hover{opacity:.8}
.file-item{display:grid;grid-template-columns:30px 1fr 100px 80px 100px;padding:10px 15px;border-bottom:1px solid var(--border);align-items:center;font-size:13px;transition:background .15s}
.file-item:last-child{border-bottom:none}
.file-item:hover{background:var(--bg)}
.file-item.selected{background:#e3f2fd}
.dark .file-item.selected{background:#1e3a5f}
.file-icon{font-size:16px;text-align:center}
.file-name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;cursor:pointer;color:var(--text)}
.file-name:hover{color:var(--primary)}
.file-size{text-align:right;color:var(--text2);font-size:12px}
.file-date{color:var(--text2);font-size:11px}
.file-actions{display:flex;justify-content:flex-end;gap:6px}
.file-action{cursor:pointer;padding:4px 6px;border-radius:4px;transition:background .15s;font-size:14px}
.file-action:hover{background:var(--border)}
.empty-msg{text-align:center;padding:40px;color:var(--text2);font-size:14px}
.modal{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,.5);z-index:1000;justify-content:center;align-items:center}
.modal-content{background:var(--card);border-radius:var(--radius);width:90%;max-width:600px;max-height:90vh;overflow-y:auto;box-shadow:var(--shadow);animation:modalIn .2s ease}
.modal-header{padding:15px 20px;border-bottom:1px solid var(--border);display:flex;justify-content:space-between;align-items:center}
.modal-header h2{font-size:16px}
.close-modal{cursor:pointer;font-size:22px;color:var(--text2);transition:color .2s}
.close-modal:hover{color:var(--danger)}
.modal-body{padding:20px}
.form-group{margin-bottom:15px}
.form-group label{display:block;margin-bottom:5px;font-weight:500;font-size:13px}
.form-group input,.form-group select{width:100%;padding:10px;border:1px solid var(--border);border-radius:6px;font-size:14px;background:var(--bg);color:var(--text)}
.form-group input:focus{outline:none;border-color:var(--primary)}
.modal-footer{padding:15px 20px;border-top:1px solid var(--border);display:flex;justify-content:flex-end;gap:8px}
.progress-bar{width:100%;height:6px;background:var(--border);border-radius:3px;margin-top:10px;overflow:hidden;display:none}
.progress-bar-fill{height:100%;background:var(--primary);width:0;transition:width .3s;border-radius:3px}
.toast{position:fixed;bottom:20px;right:20px;padding:12px 20px;border-radius:6px;color:#fff;font-size:13px;z-index:2000;opacity:0;transition:opacity .3s;max-width:300px;box-shadow:var(--shadow)}
.toast.show{opacity:1}.toast.success{background:var(--success)}.toast.error{background:var(--danger)}.toast.info{background:var(--primary)}
.preview-container{text-align:center;padding:10px}
.preview-container img{max-width:100%;max-height:60vh;border-radius:6px}
.preview-container pre{text-align:left;background:var(--bg);padding:15px;border-radius:6px;overflow-x:auto;font-size:12px;max-height:60vh;overflow-y:auto;white-space:pre-wrap;word-break:break-all}
.preview-container audio,.preview-container video{width:100%;max-height:60vh}
.drop-zone{border:2px dashed var(--border);border-radius:var(--radius);padding:30px;text-align:center;margin-bottom:12px;transition:all .2s;cursor:pointer;background:var(--card)}
.drop-zone.dragover{border-color:var(--primary);background:#e3f2fd}
.dark .drop-zone.dragover{background:#1e3a5f}
.drop-zone p{color:var(--text2);font-size:13px;margin-top:8px}
.storage-bar{height:6px;background:var(--border);border-radius:3px;overflow:hidden;margin-top:5px}
.storage-bar-fill{height:100%;background:var(--primary);border-radius:3px;transition:width .3s}
.storage-info{font-size:11px;color:var(--text2);margin-top:3px}
@keyframes modalIn{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}
@media(max-width:768px){
  .file-list-header,.file-item{grid-template-columns:28px 1fr 70px}
  .file-date,.file-size{display:none}
  .search-box input{width:120px}
  header{flex-direction:column;align-items:flex-start}
}
</style>
</head>
<body>
<div class="container">
  <header>
    <h1>📁 ESP32 File Server <small style="font-size:12px;color:var(--text2)">v2.0</small></h1>
    <div class="header-right">
      <span id="userDisplay"></span>
      <div class="search-box">🔍<input type="text" id="searchInput" placeholder="Search files..." oninput="filterFiles()"></div>
      <button class="btn btn-ghost btn-sm" id="darkToggle" onclick="toggleDark()">🌙</button>
      <a href="/trash" class="btn btn-ghost btn-sm">🗑️ Trash</a>
      <a href="/users" class="btn btn-ghost btn-sm admin-only">👥 Users</a>
      <a href="/logout" class="btn btn-sm">Logout</a>
    </div>
  </header>
  <div class="controls">
    <button class="btn" onclick="showUploadModal()">⬆️ Upload</button>
    <button class="btn" onclick="showNewFolderModal()">📁 New Folder</button>
    <button class="btn" onclick="refreshFiles()">🔄 Refresh</button>
    <button class="btn btn-danger" id="delSelBtn" style="display:none" onclick="deleteSelected()">🗑️ Delete Selected</button>
    <select class="btn btn-ghost" id="sortSelect" onchange="sortFiles()" style="padding:8px 12px">
      <option value="name-asc">Name ↑</option>
      <option value="name-desc">Name ↓</option>
      <option value="size-asc">Size ↑</option>
      <option value="size-desc">Size ↓</option>
      <option value="date-desc">Newest</option>
      <option value="date-asc">Oldest</option>
    </select>
    <div style="flex:1"></div>
    <div class="storage-info" id="storageInfo"></div>
  </div>
  <div class="path-nav" id="pathNav"><span class="path-part" data-path="/">Root</span></div>
  <div class="file-list">
    <div class="file-list-header">
      <div></div>
      <div onclick="setSort('name')">Name</div>
      <div onclick="setSort('size')" style="text-align:right">Size</div>
      <div class="file-date" onclick="setSort('date')">Date</div>
      <div style="text-align:right">Actions</div>
    </div>
    <div id="fileContainer"><div class="empty-msg">Loading...</div></div>
  </div>
</div>

<!-- Upload Modal -->
<div class="modal" id="uploadModal">
  <div class="modal-content">
    <div class="modal-header"><h2>Upload Files</h2><span class="close-modal" onclick="closeModal('uploadModal')">&times;</span></div>
    <div class="modal-body">
      <div class="drop-zone" id="dropZone" onclick="document.getElementById('fileInput').click()">
        <div style="font-size:32px">📤</div>
        <p>Drag & drop files here or click to browse</p>
        <input type="file" id="fileInput" multiple style="display:none" onchange="handleFiles(this.files)">
      </div>
      <div id="uploadList"></div>
      <div class="progress-bar" id="uploadProgress"><div class="progress-bar-fill" id="uploadProgressFill"></div></div>
    </div>
    <div class="modal-footer">
      <button class="btn btn-ghost" onclick="closeModal('uploadModal')">Close</button>
    </div>
  </div>
</div>

<!-- New Folder Modal -->
<div class="modal" id="folderModal">
  <div class="modal-content">
    <div class="modal-header"><h2>Create New Folder</h2><span class="close-modal" onclick="closeModal('folderModal')">&times;</span></div>
    <div class="modal-body">
      <div class="form-group"><label>Folder Name</label><input type="text" id="folderName" placeholder="Enter folder name"></div>
    </div>
    <div class="modal-footer">
      <button class="btn btn-ghost" onclick="closeModal('folderModal')">Cancel</button>
      <button class="btn" onclick="createFolder()">Create</button>
    </div>
  </div>
</div>

<!-- Rename Modal -->
<div class="modal" id="renameModal">
  <div class="modal-content">
    <div class="modal-header"><h2>Rename</h2><span class="close-modal" onclick="closeModal('renameModal')">&times;</span></div>
    <div class="modal-body">
      <div class="form-group"><label>New Name</label><input type="text" id="renameInput"></div>
      <input type="hidden" id="renamePath">
    </div>
    <div class="modal-footer">
      <button class="btn btn-ghost" onclick="closeModal('renameModal')">Cancel</button>
      <button class="btn" onclick="doRename()">Rename</button>
    </div>
  </div>
</div>

<!-- Preview Modal -->
<div class="modal" id="previewModal">
  <div class="modal-content" style="max-width:900px">
    <div class="modal-header"><h2 id="previewTitle">Preview</h2><span class="close-modal" onclick="closeModal('previewModal')">&times;</span></div>
    <div class="modal-body"><div class="preview-container" id="previewContent"></div></div>
  </div>
</div>

<!-- Confirm Modal -->
<div class="modal" id="confirmModal">
  <div class="modal-content" style="max-width:400px">
    <div class="modal-header"><h2>Confirm</h2><span class="close-modal" onclick="closeModal('confirmModal')">&times;</span></div>
    <div class="modal-body"><p id="confirmMsg"></p></div>
    <div class="modal-footer">
      <button class="btn btn-ghost" onclick="closeModal('confirmModal')">Cancel</button>
      <button class="btn btn-danger" id="confirmBtn">Delete</button>
    </div>
  </div>
</div>

<div class="toast" id="toast"></div>

<script>
let currentPath='/',files=[],selectedFiles=[],userLevel='user',sortBy='name',sortAsc=true;
const token=getToken();

document.addEventListener('DOMContentLoaded',()=>{
  // Dark mode from localStorage
  if(localStorage.getItem('dark')==='1'){document.body.classList.add('dark');document.getElementById('darkToggle').textContent='☀️';}
  loadFiles('/');
  // Drag & drop
  const dz=document.getElementById('dropZone');
  dz.addEventListener('dragover',e=>{e.preventDefault();dz.classList.add('dragover');});
  dz.addEventListener('dragleave',()=>dz.classList.remove('dragover'));
  dz.addEventListener('drop',e=>{e.preventDefault();dz.classList.remove('dragover');handleFiles(e.dataTransfer.files);});
  // Close modals on outside click
  window.onclick=e=>{if(e.target.classList.contains('modal'))e.target.style.display='none';};
  // Enter key on folder name
  document.getElementById('folderName').addEventListener('keydown',e=>{if(e.key==='Enter')createFolder();});
});

function getToken(){
  const p=new URLSearchParams(window.location.search);
  const t=p.get('token');
  if(t){document.cookie='session_token='+t+';path=/;max-age=1800';return t;}
  const c=document.cookie.split(';');
  for(let x of c){const[n,v]=x.trim().split('=');if(n==='session_token')return v;}
  return '';
}

function toggleDark(){
  document.body.classList.toggle('dark');
  const isDark=document.body.classList.contains('dark');
  localStorage.setItem('dark',isDark?'1':'0');
  document.getElementById('darkToggle').textContent=isDark?'☀️':'🌙';
}

function showToast(msg,type='info'){
  const t=document.getElementById('toast');
  t.textContent=msg;t.className='toast '+type+' show';
  setTimeout(()=>t.classList.remove('show'),3000);
}

function openModal(id){document.getElementById(id).style.display='flex';}
function closeModal(id){document.getElementById(id).style.display='none';}

function loadFiles(path){
  selectedFiles=[];
  updateSelBtn();
  fetch('/api/list?path='+encodeURIComponent(path),{headers:{'Authorization':'Bearer '+token}})
    .then(r=>{if(r.status===401){window.location.href='/login';throw new Error();}return r.json();})
    .then(data=>{
      files=data.files||[];
      currentPath=path;
      userLevel=data.userLevel||'user';
      if(userLevel==='admin')document.querySelectorAll('.admin-only').forEach(e=>e.style.display='');
      document.getElementById('userDisplay').textContent='👤 '+data.username;
      renderFiles();
      renderPathNav(path);
      if(data.storage){
        const pct=Math.round(data.storage.used/data.storage.total*100);
        document.getElementById('storageInfo').innerHTML=`💾 ${formatSize(data.storage.used)} / ${formatSize(data.storage.total)} (${pct}%)<div class="storage-bar"><div class="storage-bar-fill" style="width:${pct}%"></div></div>`;
      }
    })
    .catch(()=>{document.getElementById('fileContainer').innerHTML='<div class="empty-msg">Error loading files</div>';});
}

function renderFiles(){
  let f=files;
  const q=document.getElementById('searchInput').value.toLowerCase();
  if(q)f=f.filter(x=>x.name.toLowerCase().includes(q));
  // sort
  f.sort((a,b)=>{
    if(a.type==='dir' && b.type!=='dir')return -1;
    if(a.type!=='dir' && b.type==='dir')return 1;
    let cmp=0;
    if(sortBy==='name')cmp=a.name.localeCompare(b.name);
    else if(sortBy==='size')cmp=a.size-b.size;
    else if(sortBy==='date')cmp=a.mtime-b.mtime;
    return sortAsc?cmp:-cmp;
  });
  if(!f.length){document.getElementById('fileContainer').innerHTML='<div class="empty-msg">📂 This folder is empty</div>';return;}
  let html='';
  if(currentPath!=='/'){
    const parent=currentPath.split('/').slice(0,-2).join('/')+'/';
    html+=`<div class="file-item" onclick="loadFiles('${parent}')"><div class="file-icon">📁</div><div class="file-name">..</div><div class="file-size"></div><div class="file-date"></div><div class="file-actions"></div></div>`;
  }
  f.forEach(file=>{
    const sel=selectedFiles.includes(file.path);
    html+=`<div class="file-item${sel?' selected':''}" data-path="${file.path}">
      <div class="file-icon">${file.type==='dir'?'📁':file.icon}</div>
      <div class="file-name" onclick="${file.type==='dir'?`loadFiles('${file.path}')`:`previewFile('${file.path}')`}">${file.name}</div>
      <div class="file-size">${file.type==='dir'?'':formatSize(file.size)}</div>
      <div class="file-date">${file.mtime?new Date(file.mtime*1000).toLocaleDateString():''}</div>
      <div class="file-actions">
        <span class="file-action" onclick="showRenameModal('${file.path}','${file.name}')" title="Rename">✏️</span>
        ${file.type!=='dir'?`<span class="file-action" onclick="downloadFile('${file.path}')" title="Download">⬇️</span>`:''}
        <span class="file-action" onclick="deleteItem('${file.path}','${file.name}')" title="Delete">🗑️</span>
      </div>
    </div>`;
  });
  document.getElementById('fileContainer').innerHTML=html;
  // Right-click selection
  document.querySelectorAll('.file-item').forEach(item=>{
    if(!item.dataset.path)return;
    item.addEventListener('contextmenu',e=>{e.preventDefault();toggleSel(item);});
  });
}

function filterFiles(){renderFiles();}
function setSort(s){if(sortBy===s)sortAsc=!sortAsc;else{sortBy=s;sortAsc=true;}renderFiles();}
function sortFiles(){const v=document.getElementById('sortSelect').value;const[p,d]=v.split('-');sortBy=p;sortAsc=d==='asc';renderFiles();}

function toggleSel(item){
  const path=item.dataset.path;
  if(selectedFiles.includes(path)){selectedFiles=selectedFiles.filter(f=>f!==path);item.classList.remove('selected');}
  else{selectedFiles.push(path);item.classList.add('selected');}
  updateSelBtn();
}
function updateSelBtn(){
  const b=document.getElementById('delSelBtn');
  if(selectedFiles.length>0){b.style.display='';b.textContent='🗑️ Delete Selected ('+selectedFiles.length+')';}
  else b.style.display='none';
}

function renderPathNav(path){
  const parts=path.split('/').filter(p=>p);
  let html='<span class="path-part" onclick="loadFiles(\'/\')">Root</span>',build='/';
  parts.forEach(p=>{build+=p+'/';html+=`<span class="separator">/</span><span class="path-part" onclick="loadFiles('${build}')">${p}</span>`;});
  document.getElementById('pathNav').innerHTML=html;
}

function formatSize(b){
  if(b<1024)return b+' B';
  if(b<1048576)return(b/1024).toFixed(1)+' KB';
  if(b<1073741824)return(b/1048576).toFixed(1)+' MB';
  return(b/1073741824).toFixed(1)+' GB';
}

function showUploadModal(){document.getElementById('uploadList').innerHTML='';document.getElementById('uploadProgress').style.display='none';document.getElementById('uploadProgressFill').style.width='0%';openModal('uploadModal');}
function showNewFolderModal(){document.getElementById('folderName').value='';openModal('folderModal');document.getElementById('folderName').focus();}

function handleFiles(fileList){
  const list=document.getElementById('uploadList');
  list.innerHTML='';
  Array.from(fileList).forEach(f=>{
    list.innerHTML+=`<div style="display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid var(--border);font-size:13px"><span>${f.name}</span><span id="ul-${f.name.replace(/[^a-zA-Z0-9]/g,'_')}">⏳</span></div>`;
  });
  uploadFiles(fileList,0);
}

function uploadFiles(fileList,i){
  if(i>=fileList.length){showToast('All files uploaded!','success');setTimeout(()=>{closeModal('uploadModal');loadFiles(currentPath);},1500);return;}
  const file=fileList[i];
  const id='ul-'+file.name.replace(/[^a-zA-Z0-9]/g,'_');
  fetch('/api/upload-auth',{headers:{'Authorization':'Bearer '+token}})
    .then(r=>r.json())
    .then(data=>{
      const fd=new FormData();
      fd.append('file',file);
      fd.append('path',currentPath);
      const xhr=new XMLHttpRequest();
      document.getElementById('uploadProgress').style.display='block';
      xhr.upload.onprogress=e=>{if(e.lengthComputable)document.getElementById('uploadProgressFill').style.width=(e.loaded/e.total*100)+'%';};
      xhr.onload=()=>{
        document.getElementById(id).textContent=xhr.status===200?'✅':'❌';
        uploadFiles(fileList,i+1);
      };
      xhr.onerror=()=>{document.getElementById(id).textContent='❌';uploadFiles(fileList,i+1);};
      xhr.open('POST','/api/upload?token='+data.token);
      xhr.send(fd);
    })
    .catch(()=>{document.getElementById(id).textContent='❌';uploadFiles(fileList,i+1);});
}

function createFolder(){
  const name=document.getElementById('folderName').value.trim();
  if(!name){showToast('Enter a folder name','error');return;}
  fetch('/api/create-dir',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'path='+encodeURIComponent(currentPath+name)})
    .then(r=>{if(!r.ok)throw new Error();return r.text();})
    .then(()=>{showToast('Folder created','success');closeModal('folderModal');loadFiles(currentPath);})
    .catch(()=>showToast('Failed to create folder','error'));
}

function showRenameModal(path,name){
  document.getElementById('renamePath').value=path;
  document.getElementById('renameInput').value=name;
  openModal('renameModal');
  document.getElementById('renameInput').focus();
  document.getElementById('renameInput').select();
}

function doRename(){
  const path=document.getElementById('renamePath').value;
  const name=document.getElementById('renameInput').value.trim();
  if(!name){showToast('Enter a name','error');return;}
  fetch('/api/rename',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','Authorization':'Bearer '+token},body:'path='+encodeURIComponent(path)+'&name='+encodeURIComponent(name)})
    .then(r=>{if(!r.ok)throw new Error();return r.text();})
    .then(()=>{showToast('Renamed','success');closeModal('renameModal');loadFiles(currentPath);})
    .catch(()=>showToast('Failed to rename','error'));
}

function previewFile(path){
  const ext=path.split('.').pop().toLowerCase();
  const previewable=['jpg','jpeg','png','gif','bmp','svg','webp','txt','html','htm','css','js','json','xml','md','csv','mp3','wav','ogg','mp4','mov','avi'];
  if(!previewable.includes(ext)){downloadFile(path);return;}
  const type=['jpg','jpeg','png','gif','bmp','svg','webp'].includes(ext)?'image':['mp3','wav','ogg'].includes(ext)?'audio':['mp4','mov','avi'].includes(ext)?'video':'text';
  const content=document.getElementById('previewContent');
  document.getElementById('previewTitle').textContent=path.split('/').pop();
  if(type==='image'){content.innerHTML=`<img src="${path}?token=${token}" alt="preview">`;}
  else if(type==='audio'){content.innerHTML=`<audio controls src="${path}?token=${token}"></audio>`;}
  else if(type==='video'){content.innerHTML=`<video controls src="${path}?token=${token}"></video>`;}
  else{
    fetch(path+'?token='+token).then(r=>r.text()).then(t=>{content.innerHTML=`<pre>${t.replace(/</g,'&lt;').replace(/>/g,'&gt;')}</pre>`;})
    .catch(()=>{content.innerHTML='<p>Cannot preview this file</p>';});
  }
  openModal('previewModal');
}

function downloadFile(path){
  window.location.href='/api/download?path='+encodeURIComponent(path)+'&token='+token;
}

function deleteItem(path,name){
  document.getElementById('confirmMsg').textContent=`Move "${name}" to trash?`;
  document.getElementById('confirmBtn').onclick=()=>{
    fetch('/api/delete?path='+encodeURIComponent(path),{method:'DELETE',headers:{'Authorization':'Bearer '+token}})
      .then(r=>{if(!r.ok)throw new Error();return r.text();})
      .then(()=>{showToast('Moved to trash','success');closeModal('confirmModal');loadFiles(currentPath);})
      .catch(()=>{showToast('Failed to delete','error');closeModal('confirmModal');});
  };
  openModal('confirmModal');
}

function deleteSelected(){
  if(!selectedFiles.length)return;
  document.getElementById('confirmMsg').textContent=`Move ${selectedFiles.length} items to trash?`;
  document.getElementById('confirmBtn').onclick=()=>{
    Promise.all(selectedFiles.map(p=>fetch('/api/delete?path='+encodeURIComponent(p),{method:'DELETE',headers:{'Authorization':'Bearer '+token}})))
      .then(()=>{showToast('Moved to trash','success');closeModal('confirmModal');loadFiles(currentPath);})
      .catch(()=>{showToast('Some items failed','error');closeModal('confirmModal');loadFiles(currentPath);});
  };
  openModal('confirmModal');
}

function refreshFiles(){loadFiles(currentPath);}
</script>
</body>
</html>
)rawliteral";

// Minimal login page
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
input[type="text"],input[type="password"]{width:100%;padding:12px;border:1px solid #ddd;border-radius:8px;font-size:15px;transition:border .2s}
input:focus{outline:none;border-color:var(--primary)}
.btn{width:100%;background:var(--primary);color:#fff;border:none;padding:12px;border-radius:8px;cursor:pointer;font-size:15px;font-weight:600;margin-top:5px;transition:background .2s}
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
