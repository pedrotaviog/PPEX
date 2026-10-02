document.getElementById('btn-connect').addEventListener('click', conectar);

let charRX = null;
let charTX = null;
let estadoAtual = 'X'; 

const SERVICE_UUID = "00001234-0000-1000-8000-00805f9b34fb";
const RX_UUID = "0000abcd-0000-1000-8000-00805f9b34fb";
const TX_UUID = "0000dcba-0000-1000-8000-00805f9b34fb"; 

async function conectar() {
    try {
        const elStatus = document.getElementById('status');
        elStatus.innerText = "Procurando...";
        elStatus.style.color = "#94a3b8";
        
        const device = await navigator.bluetooth.requestDevice({
            filters: [{ name: 'Bot-afogo' }],
            optionalServices: [SERVICE_UUID]
        });

        const server = await device.gatt.connect();
        const service = await server.getPrimaryService(SERVICE_UUID);
        
        // Pega os canais de Escrita (RX) e Leitura (TX)
        charRX = await service.getCharacteristic(RX_UUID);
        charTX = await service.getCharacteristic(TX_UUID);
        
        // Ativa o recebimento de notificações do Radar/Velocidade/Bateria
        await charTX.startNotifications();
        charTX.addEventListener('characteristicvaluechanged', atualizarRadar);
        
        elStatus.innerText = "CONECTADO";
        elStatus.style.color = "#10b981"; 
    } catch(erro) {
        console.error(erro);
        document.getElementById('status').innerText = "FALHA NA CONEXÃO";
        document.getElementById('status').style.color = "#ef4444"; 
    }
}

async function enviarCmd(cmd) {
    if(charRX && cmd !== estadoAtual) {
        try {
            await charRX.writeValueWithoutResponse(new TextEncoder().encode(cmd));
            estadoAtual = cmd;
        } catch(e) { console.log("Erro BT: ", e); }
    }
}

function atualizarRadar(event) {
    let pacote = new TextDecoder().decode(event.target.value);
    let dados = pacote.split(';');
    if (dados.length === 3) {
        document.getElementById('dist-val').innerText = dados[0]; 
        document.getElementById('speed-val').innerText = dados[1]; 
        document.getElementById('battery-val').innerText = dados[2]; 
    }
}

// --- LÓGICA DO BOTÃO DE CHUTE ---
const btnChute = document.getElementById('btn-chute');
btnChute.addEventListener('pointerdown', () => enviarCmd('R'));
btnChute.addEventListener('pointerup', () => enviarCmd('X'));
btnChute.addEventListener('pointercancel', () => enviarCmd('X'));

// --- LÓGICA DO JOYSTICK ANALÓGICO ---
const base = document.getElementById('joystick-base');
const stick = document.getElementById('joystick-stick');

let isDragging = false;
const maxRadius = 65; 

base.addEventListener('pointerdown', (e) => {
    isDragging = true;
    stick.style.transition = 'none'; 
    moverJoystick(e);
});

window.addEventListener('pointermove', (e) => {
    if (isDragging) moverJoystick(e);
});

window.addEventListener('pointerup', soltarJoystick);
window.addEventListener('pointercancel', soltarJoystick);

function moverJoystick(e) {
    const rect = base.getBoundingClientRect();
    const centerX = rect.left + rect.width / 2;
    const centerY = rect.top + rect.height / 2;

    let dx = e.clientX - centerX;
    let dy = e.clientY - centerY;
    
    const distance = Math.hypot(dx, dy);

    if (distance > maxRadius) {
        dx = (dx / distance) * maxRadius;
        dy = (dy / distance) * maxRadius;
    }

    stick.style.transform = `translate(${dx}px, ${dy}px)`;

    let cmd = 'X';

    if (distance > 20) { 
        const angle = Math.atan2(dy, dx) * (180 / Math.PI);

        if (angle >= -135 && angle <= -45) { cmd = 'W'; } 
        else if (angle >= 45 && angle <= 135) { cmd = 'S'; } 
        else if (angle > -45 && angle < 45) { cmd = 'D'; } 
        else { cmd = 'A'; } 
    }

    enviarCmd(cmd);
}

function soltarJoystick() {
    if (!isDragging) return;
    isDragging = false;
    
    stick.style.transition = 'transform 0.2s ease-out';
    stick.style.transform = `translate(0px, 0px)`;
    
    enviarCmd('X');
}