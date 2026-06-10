// PiTrac projector / placement-zone display
//
// Renders per-club ball placement zones on a corner-pinned surface so a
// projector aimed at the hitting mat shows where to tee the ball for the
// currently selected club, and floods the zone with the system state color
// (red = no ball, orange = stabilizing, green = armed/swing away).
//
// Layout (zone positions and the four corner-pin points) is edited live on
// this page ("Edit layout") and stored via /api/projector/config.

const SURFACE_SIZE = 1000; // native (pre-warp) surface dimensions in px

let config = null;
let currentClub = 'driver';
let editing = false;
let ws = null;

// ---------------------------------------------------------------------------
// Corner-pin math: solve the homography that maps the unit square to the four
// destination corners, expressed as a CSS matrix3d.

function adj(m) {
    return [
        m[4] * m[8] - m[5] * m[7], m[2] * m[7] - m[1] * m[8], m[1] * m[5] - m[2] * m[4],
        m[5] * m[6] - m[3] * m[8], m[0] * m[8] - m[2] * m[6], m[2] * m[3] - m[0] * m[5],
        m[3] * m[7] - m[4] * m[6], m[1] * m[6] - m[0] * m[7], m[0] * m[4] - m[1] * m[3],
    ];
}

function multmm(a, b) {
    const c = [];
    for (let i = 0; i < 3; i++) {
        for (let j = 0; j < 3; j++) {
            let cij = 0;
            for (let k = 0; k < 3; k++) {
                cij += a[3 * i + k] * b[3 * k + j];
            }
            c[3 * i + j] = cij;
        }
    }
    return c;
}

function multmv(m, v) {
    return [
        m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
        m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
        m[6] * v[0] + m[7] * v[1] + m[8] * v[2],
    ];
}

function basisToPoints(x1, y1, x2, y2, x3, y3, x4, y4) {
    const m = [x1, x2, x3, y1, y2, y3, 1, 1, 1];
    const v = multmv(adj(m), [x4, y4, 1]);
    return multmm(m, [v[0], 0, 0, 0, v[1], 0, 0, 0, v[2]]);
}

function general2DProjection(x1s, y1s, x1d, y1d, x2s, y2s, x2d, y2d, x3s, y3s, x3d, y3d, x4s, y4s, x4d, y4d) {
    const s = basisToPoints(x1s, y1s, x2s, y2s, x3s, y3s, x4s, y4s);
    const d = basisToPoints(x1d, y1d, x2d, y2d, x3d, y3d, x4d, y4d);
    return multmm(d, adj(s));
}

function applyCornerPin() {
    const w = window.innerWidth;
    const h = window.innerHeight;
    const c = config.corners.map(p => [p[0] * w, p[1] * h]);

    let t = general2DProjection(
        0, 0, c[0][0], c[0][1],
        SURFACE_SIZE, 0, c[1][0], c[1][1],
        SURFACE_SIZE, SURFACE_SIZE, c[2][0], c[2][1],
        0, SURFACE_SIZE, c[3][0], c[3][1]
    );

    for (let i = 0; i < 9; i++) {
        t[i] /= t[8];
    }
    const m = [
        t[0], t[3], 0, t[6],
        t[1], t[4], 0, t[7],
        0, 0, 1, 0,
        t[2], t[5], 0, t[8],
    ];
    document.getElementById('surface').style.transform = 'matrix3d(' + m.join(',') + ')';

    // Position the edit handles at the corners
    for (let i = 0; i < 4; i++) {
        const handle = document.getElementById('corner-' + i);
        handle.style.left = c[i][0] + 'px';
        handle.style.top = c[i][1] + 'px';
    }
}

// ---------------------------------------------------------------------------
// Zones

function renderZones() {
    const surface = document.getElementById('surface');
    surface.querySelectorAll('.zone').forEach(z => z.remove());

    config.zones.forEach((zone, index) => {
        const div = document.createElement('div');
        div.className = 'zone';
        div.dataset.club = zone.club;
        div.dataset.index = index;
        div.style.left = (zone.x * SURFACE_SIZE) + 'px';
        div.style.top = (zone.y * SURFACE_SIZE) + 'px';
        div.style.width = (zone.w * SURFACE_SIZE) + 'px';
        div.style.height = (zone.h * SURFACE_SIZE) + 'px';
        div.textContent = zone.label;

        const resize = document.createElement('div');
        resize.className = 'zone-resize';
        div.appendChild(resize);

        div.addEventListener('pointerdown', (e) => {
            if (editing) {
                startZoneDrag(e, index, e.target === resize);
            }
        });
        div.addEventListener('click', () => {
            if (!editing) {
                setClub(zone.club);
            }
        });

        surface.appendChild(div);
    });

    highlightClub();
}

function highlightClub() {
    document.querySelectorAll('.zone').forEach(z => {
        z.classList.toggle('active', z.dataset.club === currentClub);
    });
}

// ---------------------------------------------------------------------------
// System state (same WebSocket feed the dashboard uses)

function setState(state, bannerText) {
    document.body.dataset.state = state;
    document.getElementById('state-banner').textContent = bannerText;
}

function handleStatus(data) {
    const type = (data.result_type || '').toLowerCase();
    const message = data.message || '';

    if (type.includes('ball placed')) {
        setState('ready', 'SWING AWAY');
    } else if (type.includes('stabilize') || type.includes('multiple')) {
        setState('stabilizing', 'HOLD ON...');
    } else if (type.includes('waiting for ball')) {
        setState('waiting', 'PLACE BALL');
    } else if (type === 'hit') {
        if (message.toLowerCase().includes('club type')) {
            // A club change rides on a zero-data Hit message - refresh selection
            refreshClub();
            return;
        }
        setState('hit', (data.speed || 0) + ' MPH');
    } else if (type.includes('error')) {
        setState('waiting', message.toLowerCase().includes('misread') ? 'MISREAD - AGAIN' : 'PLACE BALL');
    }
}

function connectWebSocket() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    ws = new WebSocket(proto + '://' + location.host + '/ws');
    ws.onmessage = (event) => {
        try {
            handleStatus(JSON.parse(event.data));
        } catch (e) { /* ignore malformed frames */ }
    };
    ws.onclose = () => setTimeout(connectWebSocket, 3000);
}

// ---------------------------------------------------------------------------
// Club selection

async function refreshClub() {
    try {
        const resp = await fetch('/api/club');
        if (resp.ok) {
            currentClub = (await resp.json()).club;
            highlightClub();
        }
    } catch (e) { /* retry on next poll */ }
}

async function setClub(club) {
    try {
        const resp = await fetch('/api/club', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ club: club }),
        });
        if (resp.ok) {
            currentClub = club;
            highlightClub();
        }
    } catch (e) { console.error('Could not set club:', e); }
}

// ---------------------------------------------------------------------------
// Edit mode: drag the blue circles to corner-pin the surface onto the mat,
// drag zones to move them, drag a zone's yellow handle to resize it.

function toggleEdit() {
    editing = !editing;
    document.body.classList.toggle('editing', editing);
    document.getElementById('btn-edit').textContent = editing ? 'Cancel' : 'Edit layout';
    document.getElementById('btn-save').style.display = editing ? 'inline-block' : 'none';
    if (!editing) {
        loadConfig(); // discard unsaved changes
    }
}

async function saveConfig() {
    try {
        const resp = await fetch('/api/projector/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config),
        });
        if (resp.ok) {
            editing = false;
            document.body.classList.remove('editing');
            document.getElementById('btn-edit').textContent = 'Edit layout';
            document.getElementById('btn-save').style.display = 'none';
        }
    } catch (e) { console.error('Could not save layout:', e); }
}

function setupCornerDrag() {
    for (let i = 0; i < 4; i++) {
        const handle = document.getElementById('corner-' + i);
        handle.addEventListener('pointerdown', (downEvent) => {
            downEvent.preventDefault();
            handle.setPointerCapture(downEvent.pointerId);

            const move = (e) => {
                config.corners[i] = [
                    Math.min(1, Math.max(0, e.clientX / window.innerWidth)),
                    Math.min(1, Math.max(0, e.clientY / window.innerHeight)),
                ];
                applyCornerPin();
            };
            const up = () => {
                handle.removeEventListener('pointermove', move);
                handle.removeEventListener('pointerup', up);
            };
            handle.addEventListener('pointermove', move);
            handle.addEventListener('pointerup', up);
        });
    }
}

function startZoneDrag(downEvent, index, isResize) {
    downEvent.preventDefault();
    downEvent.stopPropagation();

    const zone = config.zones[index];
    const div = document.querySelector('.zone[data-index="' + index + '"]');
    div.setPointerCapture(downEvent.pointerId);

    // Pointer movement happens in screen space, but the surface is warped, so
    // estimate the surface-space scale from the surface's bounding box
    const rect = document.getElementById('surface').getBoundingClientRect();
    const scaleX = rect.width / SURFACE_SIZE;
    const scaleY = rect.height / SURFACE_SIZE;

    const startX = downEvent.clientX;
    const startY = downEvent.clientY;
    const orig = { x: zone.x, y: zone.y, w: zone.w, h: zone.h };

    const move = (e) => {
        const dx = (e.clientX - startX) / (scaleX * SURFACE_SIZE);
        const dy = (e.clientY - startY) / (scaleY * SURFACE_SIZE);
        if (isResize) {
            zone.w = Math.min(1, Math.max(0.05, orig.w + dx));
            zone.h = Math.min(1, Math.max(0.05, orig.h + dy));
        } else {
            zone.x = Math.min(0.95, Math.max(0, orig.x + dx));
            zone.y = Math.min(0.95, Math.max(0, orig.y + dy));
        }
        div.style.left = (zone.x * SURFACE_SIZE) + 'px';
        div.style.top = (zone.y * SURFACE_SIZE) + 'px';
        div.style.width = (zone.w * SURFACE_SIZE) + 'px';
        div.style.height = (zone.h * SURFACE_SIZE) + 'px';
    };
    const up = () => {
        div.removeEventListener('pointermove', move);
        div.removeEventListener('pointerup', up);
    };
    div.addEventListener('pointermove', move);
    div.addEventListener('pointerup', up);
}

// ---------------------------------------------------------------------------

async function loadConfig() {
    const resp = await fetch('/api/projector/config');
    config = await resp.json();
    applyCornerPin();
    renderZones();
}

document.addEventListener('DOMContentLoaded', () => {
    loadConfig().then(() => {
        setupCornerDrag();
        connectWebSocket();
        refreshClub();
        setInterval(refreshClub, 3000);
    });
    window.addEventListener('resize', () => { if (config) applyCornerPin(); });
});
