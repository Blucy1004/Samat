(() => {
  const $ = (s, root = document) => root.querySelector(s);
  const $$ = (s, root = document) => [...root.querySelectorAll(s)];
  const editor = $('#source-editor');
  const highlight = $('#highlight-layer');
  const lineNumbers = $('#line-numbers');
  const output = $('#console-output');
  const runtimeOutput = $('#runtime-output');
  const tokenPattern = /(#.*$|f?"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\b(?:0[xX][\da-fA-F_]+|0[bB][01_]+|\d+(?:\.\d+)?)\b|[\p{L}_][\p{L}\p{N}_]*|==|!=|<=|>=|\+=|-=|\*=|\/=|&&|\|\||\?\?|->|\.\.|[^\s])/gmu;
  const keywords = new Set(['let','const','fn','if','else','while','for','in','return','break','continue','true','false','null','and','or','not','import','on','enum','struct','함수','변수','상수','라면','아니라면','동안','반환한다','반복하며','반복을','멈춘다','다음','진행한다','가져온다','모듈','실행한다','출력한다','참','거짓','시작할','때']);
  const builtins = new Set(['print','println','assert','len','length','abs','min','max','round','floor','ceil','sqrt','pow','sin','cos','tan','int','float','string','bool','vector2','vector3','color','range','append','push','pop','clear','Vector2','Vector3','Color','keyDown','deltaTime','touching']);
  const defaultScript = (x, y) => `let x = ${Math.round(x)}.0\nlet y = ${Math.round(y)}.0\nlet speed = 180.0\non start:\n    print("${'${object}'} 준비 완료")\non update:\n    if keyDown("a"):\n        x -= speed * deltaTime()\n    if keyDown("d"):\n        x += speed * deltaTime()\n    if keyDown("w"):\n        y -= speed * deltaTime()\n    if keyDown("s"):\n        y += speed * deltaTime()\n`;
  const saved = localStorage.getItem('samat-studio-project-v1');
  let project = { name: '새 프로젝트', objects: [] };
  try { if (saved) project = JSON.parse(saved); } catch { /* recover with an empty project */ }
  project.objects ||= [];
  let nextId = project.objects.reduce((m, object) => Math.max(m, Number(object.id) || 0), 0) + 1;
  let selectedId = project.objects[0]?.id ?? null;
  let activePage = 'code';
  let activeMode = localStorage.getItem('samat-studio-mode') || 'single';
  let engineRunning = false;
  let dirty = false;
  let keys = new Set();
  let frame = 0;
  let drag = null;

  const bridge = (text) => window.chrome?.webview?.postMessage(text);
  const esc = (s) => String(s).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;').replaceAll("'",'&#39;');
  function setDirty(value) { dirty = value; $('#dirty-indicator').classList.toggle('visible', value); if(activeMode==='objects')bridge(`project-dirty\n${value}`); }
  function showOutput(text, state = '완료') {
    output.textContent = text; runtimeOutput.textContent = text;
    $('#console-state').textContent = state; $('#runtime-state').textContent = state;
  }
  function persist() { localStorage.setItem('samat-studio-project-v1', JSON.stringify(project)); }
  function selectedObject() { return project.objects.find(object => object.id === selectedId) || null; }
  function updateEditorView() {
    const value = editor.value; let html = ''; let last = 0;
    for (const match of value.matchAll(tokenPattern)) {
      const token = match[0], start = match.index; html += esc(value.slice(last, start));
      const kind = token.startsWith('#') ? 'tok-comment' : /^(f?"|'|\")/.test(token) ? 'tok-string' : /^(?:0[xX][\da-fA-F_]+|0[bB][01_]+|\d)/.test(token) ? 'tok-number' : keywords.has(token) ? (/\p{Script=Hangul}/u.test(token) ? 'tok-korean' : 'tok-key') : builtins.has(token) ? 'tok-builtin' : /^[\p{L}_]/u.test(token) ? '' : 'tok-operator';
      html += kind ? `<span class="${kind}">${esc(token)}</span>` : esc(token); last = start + token.length;
    }
    highlight.innerHTML = `${html}${esc(value.slice(last))}\n`;
    lineNumbers.textContent = Array.from({length:Math.max(1,value.split('\n').length)}, (_, i) => i + 1).join('\n');
    const before = value.slice(0, editor.selectionStart).split('\n');
    $('#cursor-position').textContent = `Ln ${before.length}, Col ${before.at(-1).length + 1}`;
  }
  function syncEditorToObject() {
    if (activeMode !== 'objects') { bridge(`source\n${editor.value}`); updateEditorView(); return; }
    const object = selectedObject(); if (!object) return;
    object.script = editor.value; persist(); setDirty(true);
    bridge(`engine-source\n${editor.value}`); updateEditorView();
  }
  function selectObject(id) {
    syncEditorToObject(); selectedId = id;
    const object = selectedObject();
    editor.value = object?.script || '';
    const label = object ? `${object.name}.st` : '오브젝트 없음';
    $('#active-file').textContent = label; $('#breadcrumb-file').textContent = label; $('#tab-file').textContent = label;
    $('#inspector-file').textContent = object?.name || '오브젝트를 선택해 주세요';
    updateEditorView(); renderObjects(); renderInspector(); drawStage();
    bridge(`engine-source\n${editor.value}`);
  }
  function updateProjectLabels() {
    $('#active-file').textContent = project.name;
    $('#breadcrumb-project').textContent = project.name;
    $('#runtime-project-name').textContent = project.name;
    $('#stage-name').textContent = project.name;
    $('#scene-context').textContent = `${project.objects.length}개 오브젝트`;
    $('#stage-object-count').textContent = `${project.objects.length}개 오브젝트`;
  }
  function renderObjects() {
    const list = $('#object-list'), filter = ($('#example-filter').value || '').trim().toLocaleLowerCase(); list.replaceChildren();
    for (const object of project.objects.filter(item => item.name.toLocaleLowerCase().includes(filter))) {
      const button = document.createElement('button'); button.className = `example-card object-card${object.id === selectedId ? ' active' : ''}`;
      button.dataset.objectId = object.id;
      const swatch = document.createElement('span'); swatch.className = 'object-swatch'; swatch.style.background = object.color;
      const details = document.createElement('span'); details.innerHTML = `<b>${esc(object.name)}</b><small>Sprite · ${esc(object.scriptFile || `${object.name}.st`)}</small>`;
      button.append(swatch, details); list.append(button);
    }
    if (!project.objects.length) {
      const empty = document.createElement('div'); empty.className='empty-inspector object-empty'; empty.innerHTML='<span class="empty-icon">◇</span><b>아직 오브젝트가 없습니다</b><small>오브젝트 추가로 스프라이트를 만드세요.</small>'; list.append(empty);
    }
    updateProjectLabels();
  }
  function renderInspector() {
    const panel = $('#object-inspector'), object = selectedObject(); panel.replaceChildren();
    if (!object) { panel.innerHTML='<div class="empty-inspector"><span class="empty-icon">◇</span><b>오브젝트를 선택해 주세요</b><small>오브젝트의 위치, 크기,<br>색상을 여기서 바꿉니다.</small></div>'; return; }
    const field = (label, key, type='number', step='1') => {
      const wrap=document.createElement('label'); wrap.className='property-field'; wrap.innerHTML=`<span>${label}</span>`;
      const input=document.createElement('input'); input.type=type; input.value=object[key]; if(type==='number') input.step=step;
      input.addEventListener('input',()=>{object[key]=type==='number'?Number(input.value):input.value; if(key==='name') {$('#inspector-file').textContent=object.name; $('#tab-file').textContent=`${object.name}.st`;} persist(); setDirty(true); renderObjects(); drawStage();});
      wrap.append(input); return wrap;
    };
    panel.append(field('이름','name','text'),field('X 위치','x','number','any'),field('Y 위치','y','number','any'),field('가로','width','number','any'),field('세로','height','number','any'),field('색상','color','color'));
    const remove=document.createElement('button'); remove.className='button danger compact object-delete'; remove.innerHTML='<svg><use href="#i-close"/></svg> 오브젝트 삭제';
    remove.addEventListener('click',()=>{project.objects=project.objects.filter(item=>item.id!==object.id); selectedId=project.objects[0]?.id??null; updateSourceEditor();renderObjects();renderInspector();drawStage();persist();setDirty(true);bridge(`engine-source\n${editor.value}`);}); panel.append(remove);
  }
  function addObject() {
    const id=nextId++, x=90+project.objects.length*35, y=100+project.objects.length*25;
    const object={id,name:`스프라이트 ${id}`,x,y,width:48,height:48,color:'#b99af5',scriptFile:`sprite-${id}.st`,script:''};
    object.script=defaultScript(x,y).replaceAll('${object}',object.name);
    project.objects.push(object); selectObject(id); persist(); setDirty(true); setActivePage('code'); editor.focus();
  }
  function drawStage() {
    const canvas=$('#scene-canvas'), dpr=window.devicePixelRatio||1, rect=canvas.getBoundingClientRect();
    const pw=Math.max(1,Math.floor(rect.width*dpr)), ph=Math.max(1,Math.floor(rect.height*dpr));
    if(canvas.width!==pw)canvas.width=pw;if(canvas.height!==ph)canvas.height=ph;
    const ctx=canvas.getContext('2d');ctx.setTransform(dpr,0,0,dpr,0,0);const w=rect.width,h=rect.height;
    ctx.fillStyle='#15131c';ctx.fillRect(0,0,w,h);ctx.strokeStyle='#262230';ctx.lineWidth=1;
    for(let x=0;x<w;x+=32){ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,h);ctx.stroke();}
    for(let y=0;y<h;y+=32){ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(w,y);ctx.stroke();}
    for(const object of project.objects){
      const x=Number(object.x)||0,y=Number(object.y)||0,ww=Number(object.width)||48,hh=Number(object.height)||48;
      ctx.fillStyle=object.color||'#b99af5';ctx.beginPath();ctx.roundRect(x-ww/2,y-hh/2,ww,hh,8);ctx.fill();
      if(object.id===selectedId){ctx.strokeStyle='#f4d78d';ctx.lineWidth=2;ctx.setLineDash([5,3]);ctx.strokeRect(x-ww/2-3,y-hh/2-3,ww+6,hh+6);ctx.setLineDash([]);}
      ctx.fillStyle='#ddd5e9';ctx.font='11px "Segoe UI",sans-serif';ctx.textAlign='center';ctx.fillText(object.name,x,y+hh/2+16);
    }
    updateProjectLabels();
  }
  function serializeProject() {
    const line=(value)=>`${value}\n`; let packet='SMP1\n'+line(project.name.length)+project.name+`\n`+line(project.objects.length);
    for(const object of project.objects){
      const script=object.id===selectedId?editor.value:(object.script||'');
      packet+=line(object.id)+line(object.name.length)+object.name+'\n';
      packet+=line(object.x)+line(object.y)+line(object.width)+line(object.height)+line(object.color.length)+object.color+'\n';
      packet+=line(script.length)+script+'\n';
    }
    return packet;
  }
  function parseProject(packet) {
    let at=0;const readLine=()=>{const end=packet.indexOf('\n',at);if(end<0)throw Error('프로젝트 형식이 잘못되었습니다.');const value=packet.slice(at,end);at=end+1;return value;};
    const readText=()=>{const length=Number(readLine());if(!Number.isInteger(length)||length<0)throw Error('문자열 길이가 잘못되었습니다.');const value=packet.slice(at,at+length);if(value.length!==length)throw Error('프로젝트 데이터가 잘렸습니다.');at+=length;if(packet[at]==='\r')at++;if(packet[at]==='\n')at++;return value;};
    if(readLine()!=='SMP1')throw Error('지원하지 않는 프로젝트 버전입니다.');const name=readText();const count=Number(readLine());if(!Number.isInteger(count)||count<0||count>500)throw Error('오브젝트 수가 잘못되었습니다.');const objects=[];
    for(let i=0;i<count;i++){const id=Number(readLine());const objectName=readText();const x=Number(readLine()),y=Number(readLine()),width=Number(readLine()),height=Number(readLine()),color=readText(),script=readText();objects.push({id,name:objectName,x,y,width,height,color,scriptFile:`sprite-${id}.st`,script});}
    return {name,objects};
  }
  function runEngine(){
    syncEditorToObject(); if(!project.objects.length){showOutput('오브젝트를 하나 이상 추가해 주세요.','오브젝트 없음');return;}
    bridge(`engine-run\n${serializeProject()}`);setActivePage('runtime');$('#scene-canvas').focus();engineRunning=true;showOutput('오브젝트별 Samat 런타임을 시작합니다.','실행 중');
  }
  function saveActive(){
    if(activeMode==='objects'){syncEditorToObject();bridge(`engine-save\n${serializeProject()}`);}
    else{bridge(`source\n${editor.value}`);bridge('save');}
  }
  function checkActive(){
    bridge(activeMode==='objects'?`engine-source\n${editor.value}`:`source\n${editor.value}`);bridge('check');
  }
  function runActive(){
    if(activeMode==='objects')runEngine();
    else{syncEditorToObject();bridge('run');}
  }
  function setMode(mode){
    if(mode===activeMode)return;
    if(engineRunning)bridge('engine-stop');
    activeMode=mode;localStorage.setItem('samat-studio-mode',mode);document.body.classList.toggle('single-mode',mode==='single');document.body.classList.toggle('object-mode',mode==='objects');
    $$('.mode-button').forEach(button=>button.classList.toggle('active',button.dataset.mode===mode));
    if(mode==='objects'){
      bridge('object-mode');updateSourceEditor();renderObjects();renderInspector();drawStage();bridge(`engine-source\n${editor.value}`);
      setActivePage('code');
    }else{
      setActivePage('code');bridge('single-mode');
    }
  }
  function setActivePage(name){activePage=name;$('#code-page').hidden=name!=='code';$('#runtime-page').hidden=name!=='runtime';$$('.workspace-tab').forEach(tab=>tab.classList.toggle('active',tab.dataset.page===name));if(name==='runtime')requestAnimationFrame(drawStage);}
  function updateSourceEditor(){const object=selectedObject();editor.value=object?.script||'';const label=object?`${object.name}.st`:'오브젝트 없음';$('#tab-file').textContent=label;$('#breadcrumb-file').textContent=label;$('#inspector-file').textContent=object?.name||'오브젝트를 선택해 주세요';updateEditorView();}
  editor.addEventListener('input',()=>{syncEditorToObject();});
  $('#example-filter').addEventListener('input',renderObjects);
  editor.addEventListener('scroll',()=>{highlight.scrollTop=editor.scrollTop;highlight.scrollLeft=editor.scrollLeft;lineNumbers.scrollTop=editor.scrollTop;});
  document.addEventListener('click',event=>{
    const modeButton=event.target.closest('[data-mode]');if(modeButton){setMode(modeButton.dataset.mode);return;}
    const page=event.target.closest('[data-page]');if(page){setActivePage(page.dataset.page);return;}
    const objectButton=event.target.closest('[data-object-id]');if(objectButton){selectObject(Number(objectButton.dataset.objectId));return;}
    const action=event.target.closest('[data-action]');if(!action)return;
    switch(action.dataset.action){
      case 'add-object':addObject();break;
      case 'new-file':bridge('new-file');break;
      case 'open-st':bridge('open-st');break;
      case 'save-st':saveActive();break;
      case 'run-file':runActive();break;
      case 'run-engine':runEngine();break;
      case 'stop-engine':bridge('engine-stop');engineRunning=false;showOutput('실행을 정지했습니다.','정지');break;
      case 'save-project':saveActive();break;
      case 'open-project':bridge('open-project');break;
      case 'check':checkActive();break;
      case 'convert':bridge(activeMode==='objects'?`engine-source\n${editor.value}`:`source\n${editor.value}`);bridge('convert');break;
      case 'clear-output':showOutput('','준비됨');break;
      case 'focus-search':$('#example-filter').focus();break;
    }
  });
  const canvas=$('#scene-canvas');
  canvas.addEventListener('pointerdown',event=>{const rect=canvas.getBoundingClientRect(),x=event.clientX-rect.left,y=event.clientY-rect.top;const hit=[...project.objects].reverse().find(object=>Math.abs(x-object.x)<=object.width/2&&Math.abs(y-object.y)<=object.height/2);if(hit&&!engineRunning){selectObject(hit.id);drag={id:hit.id,dx:hit.x-x,dy:hit.y-y};canvas.setPointerCapture(event.pointerId);}});
  canvas.addEventListener('pointermove',event=>{if(!drag)return;const object=selectedObject();if(!object)return;const rect=canvas.getBoundingClientRect();object.x=event.clientX-rect.left+drag.dx;object.y=event.clientY-rect.top+drag.dy;drawStage();renderInspector();persist();setDirty(true);});
  canvas.addEventListener('pointerup',()=>{drag=null;});
  window.addEventListener('keydown',event=>{
    const key=event.key.toLowerCase(), modifier=event.ctrlKey||event.metaKey;
    if(modifier&&key==='s'){event.preventDefault();saveActive();return;}
    if(modifier&&key==='enter'){event.preventDefault();checkActive();return;}
    if(key==='f5'&&event.shiftKey&&activeMode==='objects'){event.preventDefault();bridge('engine-stop');engineRunning=false;showOutput('실행을 정지했습니다.','정지');return;}
    if(key==='f5'){event.preventDefault();runActive();return;}
    if(!engineRunning||event.repeat)return;keys.add(key);bridge(`engine-key\n${key}\npressed`);
  });
  window.addEventListener('keyup',event=>{const key=event.key.toLowerCase();keys.delete(key);if(engineRunning)bridge(`engine-key\n${key}\nreleased`);});
  window.addEventListener('blur',()=>{for(const key of keys)bridge(`engine-key\n${key}\nreleased`);keys.clear();});
  window.addEventListener('resize',drawStage);
  function receive(message){
    if(message==='ready'){bridge('ready');if(activeMode==='objects')bridge(`engine-source\n${editor.value}`);return;}
    if(message.startsWith('output\n')){const stateEnd=message.indexOf('\n',7),state=message.slice(7,stateEnd),lenEnd=message.indexOf('\n',stateEnd+1),len=Number(message.slice(stateEnd+1,lenEnd));showOutput(message.slice(lenEnd+1,lenEnd+1+len),state);return;}
    if(message.startsWith('state\n')){const lengthEnd=message.indexOf('\n',6),length=Number(message.slice(6,lengthEnd)),sourceStart=lengthEnd+1;const fields=message.slice(sourceStart+length+1).split('\n');if(activeMode==='single'){editor.value=message.slice(sourceStart,sourceStart+length);$('#active-file').textContent=fields[0]||'새 문서';$('#language-mode').textContent=fields[2]||'Code Syntax';updateEditorView();}return;}
    if(message.startsWith('engine-status\n')){const [state,...body]=message.slice(14).split('\n');showOutput(body.join('\n'),state);engineRunning=state==='실행 중';if(state==='저장됨')setDirty(false);return;}
    if(message.startsWith('status\n')){const [state,...body]=message.slice(7).split('\n');showOutput(body.join('\n'),state);return;}
    if(message.startsWith('engine-state\n')){for(const row of message.slice(13).split('\n')){const [id,x,y]=row.split('|');const object=project.objects.find(item=>item.id===Number(id));if(object){object.x=Number(x);object.y=Number(y);}}persist();drawStage();return;}
    if(message.startsWith('engine-code\n')){editor.value=message.slice(12);const object=selectedObject();if(object)object.script=editor.value;updateEditorView();persist();setDirty(true);return;}
    if(message.startsWith('project-load\n')){try{project=parseProject(message.slice(13));nextId=project.objects.reduce((m,o)=>Math.max(m,o.id),0)+1;selectedId=project.objects[0]?.id??null;updateSourceEditor();renderObjects();renderInspector();drawStage();persist();setDirty(false);showOutput('프로젝트를 열었습니다.','열기 완료');}catch(error){showOutput(error.message,'프로젝트 오류');}return;}
  }
  if(window.chrome?.webview)window.chrome.webview.addEventListener('message',event=>receive(event.data));else document.body.classList.add('browser-preview');
  renderObjects();renderInspector();drawStage();document.body.classList.toggle('single-mode',activeMode==='single');document.body.classList.toggle('object-mode',activeMode==='objects');$$('.mode-button').forEach(button=>button.classList.toggle('active',button.dataset.mode===activeMode));
  if(activeMode==='objects'){updateSourceEditor();bridge('object-mode');bridge(`engine-source\n${editor.value}`);}bridge('ready');
})();
