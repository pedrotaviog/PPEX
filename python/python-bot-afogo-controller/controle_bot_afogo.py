import asyncio
from bleak import BleakClient, BleakScanner
import keyboard

ROBOT_NAME = "Bot-afogo"

RX_UUID = "0000abcd-0000-1000-8000-00805f9b34fb" 
TX_UUID = "0000dcba-0000-1000-8000-00805f9b34fb" 

def telemetria_callback(sender, data):
    try:
        pacote = data.decode("utf-8")
        distancia, velocidade, bateria = pacote.split(';')
        print(f"\r[HUD] Radar: {distancia:>3} cm  |  Vel: {velocidade:>5} cm/s  |  Bateria: {bateria:>3}%      ", end="", flush=True)
    except ValueError:
        pass

async def main():
    print(f"Procurando por {ROBOT_NAME}...")
    device = await BleakScanner.find_device_by_name(ROBOT_NAME, timeout=10.0)
    
    if not device:
        print(f"Robô '{ROBOT_NAME}' não encontrado. O ESP32 está ligado?")
        return

    print(f"Robô encontrado ({device.address}). Conectando...")
    
    async with BleakClient(device.address, timeout=30.0) as client:
        print("Conectado com sucesso!\n")
        
        await client.start_notify(TX_UUID, telemetria_callback)
        
        print("--- Controle do Bot-afogo Ativo ---")
        print("Use W/A/S/D para mover | R para chutar | ESC para sair\n")
        
        estado_atual = "X"
        
        try:
            while True:
                if keyboard.is_pressed('esc'):
                    print("\n\nSaindo do controle...")
                    break
                    
                w = keyboard.is_pressed('w')
                s = keyboard.is_pressed('s')
                a = keyboard.is_pressed('a')
                d = keyboard.is_pressed('d')
                r = keyboard.is_pressed('r')
                
                cmd = 'X'
                
                # Prioridade 1: Chute
                if r:
                    cmd = 'R'
                # Prioridade 2: Movimentos diagonais
                elif w and a: cmd = 'Q'
                elif w and d: cmd = 'E'
                elif s and a: cmd = 'Z'
                elif s and d: cmd = 'C'
                # Prioridade 3: Movimentos básicos
                elif w: cmd = 'W'
                elif s: cmd = 'S'
                elif a: cmd = 'A'
                elif d: cmd = 'D'
                
                if cmd != estado_atual:
                    await client.write_gatt_char(RX_UUID, cmd.encode("utf-8"), response=False)
                    estado_atual = cmd
                    
                    if cmd == 'R':
                        await asyncio.sleep(0.5) 
                        estado_atual = "X" 
                        
                await asyncio.sleep(0.05) 
                
        except KeyboardInterrupt:
            pass
        finally:
            await client.write_gatt_char(RX_UUID, b'X', response=False)
            await client.stop_notify(TX_UUID)
            print("\nBluetooth Desconectado. Robô parado.")

if __name__ == "__main__":
    asyncio.run(main())