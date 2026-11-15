import html,json,os,pathlib,re,subprocess,tempfile,traceback
try:
    chrome = r'C:\Program Files\Google\Chrome\Application\chrome.exe'
    root=pathlib.Path.cwd()
    fixture=(root/'tests/fixtures/p2/flex-grid.html').read_text(encoding='utf8')
    probe='''<script>const ss=['.flex-row','.card:nth-child(1)','.card:nth-child(2)','.card:nth-child(3)','.grid','.grid > div:nth-child(1)','.grid > div:nth-child(2)','.grid > div:nth-child(3)','.grid > div:nth-child(4)','.grid > div:nth-child(5)'];let r={};for(const s of ss){const v=document.querySelector(s).getBoundingClientRect();r[s]={x:v.x,y:v.y,w:v.width,h:v.height};}document.body.textContent=JSON.stringify({viewport:[innerWidth,innerHeight],boxes:r});</script>'''
    fixture=fixture.replace('</body>',probe+'</body>')
    path=pathlib.Path(tempfile.gettempdir())/'p2-probe.html'
    path.write_text(fixture,encoding='utf8')
    profile=str(path.parent/'p2-chrome-profile-1234')
    result=subprocess.run([chrome,'--headless','--disable-gpu','--no-sandbox','--window-size=1396,1000','--user-data-dir='+profile,'--dump-dom',path.as_uri()],capture_output=True,encoding='utf8',errors='replace',timeout=60)
    if result.returncode: raise Exception('chrome failed: '+str(result.returncode)+' '+result.stderr[-1000:])
    m=re.search(r'<body[^>]*>(.*?)</body>',result.stdout,re.S)
    if not m: raise Exception('bad dump '+result.stdout[-1200:]+' '+result.stderr[-500:])
    output=json.loads(html.unescape(m.group(1)))
    version=subprocess.check_output([chrome,'--version'],encoding='utf8',errors='replace').strip()
    print('::notice title=P2 browser reference::'+version+' '+json.dumps(output))
except Exception:
    error=traceback.format_exc().replace('%','%25').replace('\r','%0D').replace('\n','%0A')
    print('::error title=P2 browser probe::'+error)
    raise
