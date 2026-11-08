/* Execute the shipped page script with a minimal DOM/network harness. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../assets/web/pocket_setup.html'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const tick = () => new Promise(resolve => setImmediate(resolve));

async function page(options = {}) {
    const elements = new Map(), posts = [];
    function element(id, extra = {}) {
        const value = {id, value: '', disabled: false, checked: false, textContent: '', dataset: {}, style: {},
            addEventListener(event, callback) { this[event] = callback; }, ...extra};
        elements.set(id, value); return value;
    }
    const message = element('message'), buttons = [];
    const forms = [
        ['wifi', [['ssid', 'wifi_ssid'], ['wifi-password', 'wifi_password']]],
        ['nas', [['nas-host', 'nas_host'], ['nas-password', 'nas_community']]],
        ['home', [['home-url', 'home_url'], ['home-token', 'home_token']]],
        ['codex', [['computer-address', 'address'], ['pairing', 'code']]],
        ['pc-display', []],
        ['page-layout', []],
        ['display-settings', [['screen-timeout','screen_timeout']]],
        ['brightness-settings', [['brightness','brightness']]],
    ].map(([id, fields]) => {
        const form = element(id, {fields: fields.map(([input, name]) => element(input, {name}))});
        buttons.push(element(id + '-button')); return form;
    });
    element('open-network');buttons.push(element('screen-off'));element('brightness-value');
    const moves=[];
    for(const id of ['genshin','codex','nas','home','pc']){
        element('page-row-'+id);element('page-enabled-'+id,{checked:true});
        for(const step of [-1,1]){const move=element('move-'+id+'-'+step,{dataset:{pageId:id,pageStep:String(step)}});moves.push(move);buttons.push(move);}
    }
    const pcInputs = ['cpu','gpu','gpu_temp','memory','gpu_memory','disk','disk_free','uptime','status','memory_used_bytes','memory_free_bytes','memory_total_bytes','gpu_memory_used_bytes','gpu_memory_free_bytes','gpu_memory_total_bytes','gpu_power_w','gpu_fan_percent','gpu_clock_mhz','cpu_clock_mhz','disk_read_bps','disk_write_bps','net_rx_bps','net_tx_bps']
        .map(value=>element('pc-'+value,{name:'pc_metric',value,checked:false}));
    ['codex-state', 'nas-state', 'home-state'].forEach(id => element(id));
    const secrets = ['wifi-password', 'nas-password', 'home-token', 'pairing'].map(id => elements.get(id));
    const context = {
        document: {
            getElementById: id => elements.get(id),
            querySelector: selector => [...elements.values()].find(input => selector === '[name=' + input.name + ']'),
            querySelectorAll: selector => selector === 'button' ? buttons : selector === 'form' ? forms :
                selector === '[data-show]' ? [] : selector === '[name=pc_metric]' ? pcInputs : selector === '[data-page-step]' ? moves : secrets,
        },
        location: {search: '?k=test-session'},
        FormData: class {
            constructor(form) { this.form = form; }
            *[Symbol.iterator]() { for (const input of this.form.fields) if (!input.disabled) yield [input.name, input.value]; }
        },
        fetch: async (url, request) => {
            if (!request) return {ok: true, json: async () => ({wifi_ssid: '', nas_host: '192.168.1.250', ...options.status})};
            posts.push([url, JSON.parse(request.body)]);
            await tick();
            return {ok: !options.failure, status: options.failure || 200, json: async () => ({ok: true,restart:!url.startsWith('/screen-off')})};
        },
    };
    vm.runInNewContext(script, context);
    await tick();
    return {elements, posts, message, buttons,
        async submit(id) { elements.get(id).submit({preventDefault() {}}); await tick(); await tick(); }};
}

(async () => {
    let ui = await page();
    ui.elements.get('ssid').value = 'Home'; ui.elements.get('wifi-password').value = 'test-password';
    await ui.submit('wifi');
    assert.deepEqual(ui.posts, [['/save?k=test-session', {wifi_ssid:'Home', wifi_password:'test-password'}]]);
    assert.equal(ui.elements.get('wifi-password').value, '');
    assert(ui.buttons.every(button => button.disabled));

    ui = await page(); await ui.submit('nas');
    assert.deepEqual(ui.posts, [['/save?k=test-session', {nas_host:'192.168.1.250'}]]);
    ui = await page(); ui.elements.get('ssid').value = 'Open'; ui.elements.get('open-network').checked = true;
    await ui.submit('wifi');
    assert.deepEqual(ui.posts, [['/save?k=test-session', {wifi_ssid:'Open', wifi_password:''}]]);
    ui = await page(); ui.elements.get('ssid').value = 'Home'; await ui.submit('wifi');
    assert.equal(ui.posts.length, 0); assert(ui.message.textContent.includes('填写'));

    ui = await page(); ui.elements.get('computer-address').value = '192.168.1.20'; ui.elements.get('pairing').value = '123456';
    await ui.submit('codex');
    assert.deepEqual(ui.posts, [['/codex-pair?k=test-session', {address:'192.168.1.20', code:'123456'}]]);
    assert.equal(ui.elements.get('pairing').value, '');

    ui = await page({failure:403}); await ui.submit('nas');
    assert(ui.message.textContent.includes('会话已结束'));
    ui = await page({failure:400}); await ui.submit('codex');
    assert(ui.message.textContent.includes('配对未完成')); assert(ui.buttons.every(button => !button.disabled));
    ui = await page();
    assert.deepEqual(['cpu','gpu','gpu_temp','memory','disk','status'].map(id=>ui.elements.get('pc-'+id).checked),[true,true,true,true,true,true]);
    await ui.submit('pc-display');
    assert.deepEqual(ui.posts,[['/pc-display?k=test-session',{pc_display:'cpu,gpu,gpu_temp,memory,disk,status'}]]);
    ui = await page({status:{pc_display:'gpu_memory,disk_free,uptime'}});
    await ui.submit('pc-display');
    assert.deepEqual(ui.posts,[['/pc-display?k=test-session',{pc_display:'gpu_memory,disk_free,uptime'}]]);
    ui = await page();
    for (const input of [...ui.elements.values()].filter(input=>input.name==='pc_metric'))input.checked=false;
    await ui.submit('pc-display');assert.equal(ui.posts.length,0);assert(ui.message.textContent.includes('最多选择二十四项'));
    for (const input of [...ui.elements.values()].filter(input=>input.name==='pc_metric'))input.checked=true;
    await ui.submit('pc-display');assert.equal(ui.posts.length,1);assert(ui.posts[0][1].pc_display.split(',').length===23);
    ui = await page();
    ui = await page({status:{page_layout:'pc,-genshin,codex,-nas,home'}});
    assert.equal(ui.elements.get('page-row-pc').style.order,0);
    assert.equal(ui.elements.get('page-enabled-genshin').checked,false);
    ui.elements.get('move-codex--1').click();
    await ui.submit('page-layout');
    assert.deepEqual(ui.posts,[['/page-layout?k=test-session',{page_layout:'pc,codex,-genshin,-nas,home'}]]);
    ui=await page();
    for(const id of ['genshin','codex','nas','home','pc'])ui.elements.get('page-enabled-'+id).checked=false;
    await ui.submit('page-layout');assert.equal(ui.posts.length,0);assert(ui.message.textContent.includes('至少启用'));
    ui.elements.get('page-enabled-pc').checked=true;
    await ui.submit('page-layout');
    assert.deepEqual(ui.posts,[['/page-layout?k=test-session',{page_layout:'-genshin,-codex,-nas,-home,pc'}]]);
    ui = await page();
    ui=await page({status:{screen_timeout:120}});assert.equal(ui.elements.get('screen-timeout').value,'120');
    ui.elements.get('screen-timeout').value='30';await ui.submit('display-settings');assert.deepEqual(ui.posts,[['/display-settings?k=test-session',{screen_timeout:'30'}]]);
    ui=await page();ui.elements.get('screen-off').click();await tick();await tick();assert.deepEqual(ui.posts,[['/screen-off?k=test-session',{}]]);assert(ui.buttons.every(button=>!button.disabled));
    ui=await page({status:{brightness:35}});assert.equal(ui.elements.get('brightness').value,'35');assert.equal(ui.elements.get('brightness-value').textContent,'35%');
    ui.elements.get('brightness').value='45';ui.elements.get('brightness').input();assert.equal(ui.elements.get('brightness-value').textContent,'45%');
    assert.equal(ui.posts.length,0);await ui.submit('brightness-settings');assert.deepEqual(ui.posts,[['/brightness?k=test-session',{brightness:'45'}]]);
    assert(ui.buttons.every(button=>!button.disabled));ui.elements.get('brightness').value='100';await ui.submit('brightness-settings');assert.equal(ui.posts.length,2);
    ui=await page({status:{brightness:0}});assert.equal(ui.elements.get('brightness').value,'75');
    ui.elements.get('brightness').value='0';await ui.submit('brightness-settings');assert.equal(ui.posts.length,0);
    ui=await page({failure:400});await ui.submit('brightness-settings');assert(ui.buttons.every(button=>!button.disabled));
    ui=await page();
    const event = {preventDefault() {}};
    ui.elements.get('nas').submit(event); ui.elements.get('nas').submit(event);
    await tick(); await tick(); assert.equal(ui.posts.length, 1);
    console.log('Setup webpage script: PASS (partial updates, secrets, open Wi-Fi, pairing, PC selection/limits/restore, errors, duplicate submission)');
})().catch(error => { console.error(error); process.exitCode = 1; });
