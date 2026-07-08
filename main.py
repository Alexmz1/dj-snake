"""
Ce script capte les fleches directionnelles ET les touches ZQSD (comme dans
la plupart des jeux), plus la touche 'r' pour rejouer :
    Fleche haut / Z    -> 'U'
    Fleche bas   / S   -> 'D'
    Fleche gauche/ Q   -> 'L'
    Fleche droite/ D   -> 'R'
    Touche 'r'         -> 'X'  (reset / rejouer)
    Echap              -> quitte le script
"""

import socket
import sys
import threading
import time

from pynput import keyboard

ESP32_IP = "192.168.191.156"
ESP32_PORT = 3333

sock = None
sock_lock = threading.Lock()
connected = threading.Event()
stop_requested = threading.Event()


def connect_loop():
    """Tourne en arriere-plan : (re)connecte au TCP de l'ESP32 en continu."""
    global sock
    while not stop_requested.is_set():
        try:
            new_sock = socket.create_connection((ESP32_IP, ESP32_PORT), timeout=5)
            with sock_lock:
                sock = new_sock
            connected.set()
            print(f"Connecte a l'ESP32 ({ESP32_IP}:{ESP32_PORT}).")

            # on reste sur cette connexion tant qu'elle est valide
            while not stop_requested.is_set():
                try:
                    # ping silencieux pour detecter une deconnexion (l'ESP32 n'envoie rien lui-meme)
                    new_sock.settimeout(1)
                    data = new_sock.recv(1)
                    if data == b"":
                        raise ConnectionError("Connexion fermee par l'ESP32.")
                except socket.timeout:
                    continue
        except (OSError, ConnectionError) as exc:
            connected.clear()
            print(f"Connexion impossible ({exc}), nouvelle tentative dans 2s...")
            time.sleep(2)


def send_command(command: str):
    if connected.is_set() and sock is not None:
        try:
            with sock_lock:
                sock.sendall(command.encode("ascii"))
            print(f"Envoye : {command}")
        except Exception as exc:
            print(f"Impossible d'envoyer la commande : {exc}")
            connected.clear()
    else:
        print("Pas encore connecte a l'ESP32, commande ignoree.")


def on_press(key):
    try:
        char = getattr(key, "char", None)

        if key == keyboard.Key.up or char == "z":
            send_command("U")
        elif key == keyboard.Key.down or char == "s":
            send_command("D")
        elif key == keyboard.Key.left or char == "q":
            send_command("L")
        elif key == keyboard.Key.right or char == "d":
            send_command("R")
        elif char == "r":
            send_command("X")  # reset / rejouer (ne pas confondre avec 'R' = droite)
        elif key == keyboard.Key.esc:
            print("Arret demande (Echap).")
            return False  # stoppe le listener clavier
        else:
            print(f"(touche ignoree : {key})")
    except Exception as exc:
        print(f"Erreur de traitement de la touche : {exc}")


def main():
    print(f"Connexion a l'ESP32 sur {ESP32_IP}:{ESP32_PORT} ...")
    thread = threading.Thread(target=connect_loop, daemon=True)
    thread.start()

    if not connected.wait(timeout=10):
        print("Toujours pas connecte, le script continue d'essayer en arriere-plan...")

    print("Utilise les fleches directionnelles pour jouer, 'r' pour rejouer, Echap pour quitter.")
    with keyboard.Listener(on_press=on_press) as listener:
        listener.join()

    stop_requested.set()
    print("Script termine.")
    sys.exit(0)


if __name__ == "__main__":
    main()