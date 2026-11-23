"""USB/IP server (protocol 1.1.1) exporting the emulated device, plus the
local "stub host" duties a real usbip-host performs: bus reset and
SET_ADDRESS (vhci_hcd never forwards SET_ADDRESS or port resets)."""
import socket
import struct
import threading

VERSION = 0x0111
OP_REQ_DEVLIST, OP_REP_DEVLIST, OP_REQ_IMPORT, OP_REP_IMPORT = 0x8005, 0x0005, 0x8003, 0x0003
CMD_SUBMIT, CMD_UNLINK, RET_SUBMIT, RET_UNLINK = 1, 2, 3, 4
DIR_IN = 1
BUSID, BUSNUM, DEVNUM, SPEED_FULL = "1-1", 1, 2, 2
ECONNRESET, EPIPE = -104, -32


def recv_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("peer closed")
        buf += chunk
    return bytes(buf)


class StubHost:
    """Enumerates the device locally (as the exporting host would) and caches
    the descriptors USB/IP advertises."""

    def __init__(self, machine):
        self.m = machine
        self.device = None
        self.config = None

    def control(self, setup, out=b"", virtual_ms=200):
        result = {}

        def submit():
            self.m.otg.submit_control(setup, out, lambda st, data: result.update(status=st, data=data))

        self.m.call(submit)
        self.m.wait_until(lambda: "status" in result, virtual_ms=virtual_ms)
        if "status" not in result:
            raise TimeoutError(f"no reply to SETUP {bytes(setup).hex(' ')}")
        return result["status"], result["data"]

    def enumerate(self, address=DEVNUM):
        """Bus reset, then GET_DESCRIPTOR / SET_ADDRESS / GET_DESCRIPTOR(config)."""
        m = self.m
        m.wait_until(lambda: m.otg.connected, virtual_ms=2000)
        if not m.call(lambda: m.otg.connected):
            raise RuntimeError("firmware never connected to the bus (DCTL.SDIS still set)")
        m.call(m.otg.bus_reset)
        m.wait_until(lambda: m.otg.enumerated and not (m.otg.r(0x014) & (1 << 13)), virtual_ms=50)
        st, dev = self.control(bytes([0x80, 6, 0, 1, 0, 0, 64, 0]))
        if st:
            raise RuntimeError(f"GET_DESCRIPTOR(device) failed: {st}")
        st, _ = self.control(bytes([0x00, 5, address, 0, 0, 0, 0, 0]))
        if st:
            raise RuntimeError(f"SET_ADDRESS failed: {st}")
        st, cfg = self.control(bytes([0x80, 6, 0, 2, 0, 0, 255, 0]))
        if st:
            raise RuntimeError(f"GET_DESCRIPTOR(config) failed: {st}")
        self.device, self.config = dev, cfg
        return dev, cfg

    def device_record(self):
        d, c = self.device, self.config
        rec = struct.pack(">256s32sIII", b"/sys/devices/emulated/stm32f411/usb1/1-1",
                          BUSID.encode(), BUSNUM, DEVNUM, SPEED_FULL)
        rec += struct.pack(">HHHBBBBBB", d[8] | d[9] << 8, d[10] | d[11] << 8, d[12] | d[13] << 8,
                           d[4], d[5], d[6], 0, d[17], c[4])
        return rec

    def interfaces(self):
        out, i = b"", 0
        while i + 1 < len(self.config) and self.config[i]:
            if self.config[i + 1] == 4:
                out += bytes([self.config[i + 5], self.config[i + 6], self.config[i + 7], 0])
            i += self.config[i]
        return out


class USBIPServer(threading.Thread):
    def __init__(self, machine, stub, host="127.0.0.1", port=3240, log=print):
        super().__init__(daemon=True)
        self.m, self.stub, self.log = machine, stub, log
        self.sock = socket.create_server((host, port), reuse_port=False)
        self.urb_log = []

    def run(self):
        while True:
            conn, _ = self.sock.accept()
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def _serve(self, conn):
        try:
            version, code, _ = struct.unpack(">HHI", recv_exact(conn, 8))
            if code == OP_REQ_DEVLIST:
                body = struct.pack(">I", 1) + self.stub.device_record() + self.stub.interfaces()
                conn.sendall(struct.pack(">HHI", VERSION, OP_REP_DEVLIST, 0) + body)
                conn.close()
            elif code == OP_REQ_IMPORT:
                busid = recv_exact(conn, 32).split(b"\0")[0].decode()
                if busid != BUSID:
                    conn.sendall(struct.pack(">HHI", VERSION, OP_REP_IMPORT, 1))
                    conn.close()
                    return
                self.log("[usbip] import: bus reset + local enumeration")
                self.stub.enumerate()
                conn.sendall(struct.pack(">HHI", VERSION, OP_REP_IMPORT, 0) + self.stub.device_record())
                self._urbs(conn)
            else:
                conn.close()
        except (ConnectionError, OSError) as e:
            self.log(f"[usbip] connection ended: {e}")

    def _urbs(self, conn):
        lock = threading.Lock()

        def send(b):
            with lock:
                try:
                    conn.sendall(b)
                except OSError:
                    pass

        while True:
            hdr = recv_exact(conn, 48)
            cmd, seq, devid, direction, ep = struct.unpack(">IIIII", hdr[:20])
            if cmd == CMD_SUBMIT:
                flags, length, _sf, _np, _iv = struct.unpack(">IiiiI", hdr[20:40])
                setup = hdr[40:48]
                data = recv_exact(conn, length) if direction != DIR_IN and length > 0 else b""
                self._submit(seq, direction, ep, length, setup, data, send)
            elif cmd == CMD_UNLINK:
                target = struct.unpack(">I", hdr[20:24])[0]
                found = self.m.call(lambda: self.m.otg.unlink_ep1(target))
                send(struct.pack(">IIIIIi", RET_UNLINK, seq, 0, 0, 0, ECONNRESET if found else 0) + bytes(24))
            else:
                raise ConnectionError(f"unknown USB/IP command {cmd}")

    def _submit(self, seq, direction, ep, length, setup, data, send):
        def reply(status, payload):
            payload = payload if direction == DIR_IN else b""
            actual = len(payload) if direction == DIR_IN else (len(data) if status == 0 else 0)
            send(struct.pack(">IIIIIiiiii", RET_SUBMIT, seq, 0, 0, 0, status, actual, 0, 0, 0)
                 + bytes(8) + payload)

        if ep == 0:
            self.urb_log.append(setup.hex(" "))
            self.m.post(lambda: self.m.otg.submit_control(setup, data, reply))
        elif ep == 1 and direction == DIR_IN:
            self.m.post(lambda: self.m.otg.submit_ep1(seq, length, reply))
        else:
            reply(EPIPE, b"")
