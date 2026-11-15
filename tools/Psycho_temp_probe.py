import html,json,pathlib,re,subprocess,tempfile,traceback,shutil
try:
    chrome = r'C:\Program Files\Google\Chrome\Application\chrome.exe'
    root=pathlib.Path.cwd(); tmp=pathlib.Path(tempfile.gettempdir())
    shutil.copyfile(root/'tests/fixtures/p2/fixture.js',tmp/'fixture.js')
    cases={
        'dynamic-dom.html':['body','#target','#mutated'],
        'text-controls.html':['body','main','.label-row:nth-of-type(1)','#address','#open','.label-row:nth-of-type(2)','input[type=checkbox]','button:disabled'],
        'scroll-position.html':['body','.shell','.sticky','.scroller','.badge','.fixed-note','.scroller p:nth-child(1)','.scroller p:nth-child(10)'],
    }
    results={}
    for i,(fixture,selectors) in enumerate(cases.items()):
        source=(root/'tests/fixtures/p2'/fixture).read_text(encoding='utf8')
        source=source.replace('https://s050.test/fixture.js','fixture.js')
        probe='''<script>const ss=__SELECTORS__;let r={};for(const s of ss){let e=document.querySelector(s);if(!e){r[s]=null;continue;}const v=e.getBoundingClientRect(), c=getComputedStyle(e);r[s]={x:v.x,y:v.y,w:v.width,h:v.height,text:e.textContent,value:e.value,checked:e.checked,disabled:e.disabled,bg:c.backgroundColor};}let extra={};let sc=document.querySelector('.scroller');if(sc){let p=sc.querySelector('p');extra.before=p.getBoundingClientRect().y;sc.scrollTop=80;extra.after=p.getBoundingClientRect().y;extra.scrollTop=sc.scrollTop;extra.scrollHeight=sc.scrollHeight;extra.clientHeight=sc.clientHeight;}document.body.textContent=JSON.stringify({viewport:[innerWidth,innerHeight],boxes:r,extra:extra});</script>'''.replace('__SELECTORS__',json.dumps(selectors))
        source=source.replace('</body>',probe+'</body>')
        path=tmp/('p2-probe-'+str(i)+'.html');path.write_text(source,encoding='utf8')
        result=subprocess.run([chrome,'--headless','--disable-gpu','--no-sandbox','--allow-file-access-from-files','--window-size=1412,985','--user-data-dir='+str(tmp/('p2-probe-profile-'+str(i))),'--dump-dom',path.as_uri()],capture_output=True,encoding='utf8',errors='replace',timeout=90)
        if result.returncode: raise Exception('chrome failed: '+str(result.returncode)+' '+result.stderr[-1000:])
        m=re.search(r'<body[^>]*>(.*?)</body>',result.stdout,re.S)
        if not m: raise Exception('bad dump '+result.stdout[-1200:]+' '+result.stderr[-500:])
        results[fixture]=json.loads(html.unescape(m.group(1)))
    version=subprocess.check_output([chrome,'--version'],encoding='utf8',errors='replace').strip()
    print('::notice title=P2 browser reference::'+version+' '+json.dumps(results).replace('%','%25'))
except Exception:
    error=traceback.format_exc().replace('%','%25').replace('\r','%0D').replace('\n','%0A')
    print('::error title=P2 browser probe::'+error)
    raise
