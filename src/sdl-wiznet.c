#include <SDL_net.h>
#include <stdbool.h>
#include <stdio.h>
#include "risc-io.h"

#define MAX_WIZNET_SOCKETS 256
#define WIZNET_BUFSIZE 1024

struct WizNetTCP {
        TCPsocket sock;
        uint32_t len;
        bool closed;
        uint8_t buf[WIZNET_BUFSIZE];
};

struct WizNet {
  struct RISC_WizNet wiznet;
  SDLNet_SocketSet sockset;
  UDPsocket udpsock[MAX_WIZNET_SOCKETS];
  struct WizNetTCP* tcpsock[MAX_WIZNET_SOCKETS];
  TCPsocket listener[MAX_WIZNET_SOCKETS];
};

static void wiznet_write(const struct RISC_WizNet *wiznet_wiznet, uint32_t value, uint32_t *ram) {
  struct WizNet *wiznet = (struct WizNet *)wiznet_wiznet;

  // process pending socket activity
  if (SDLNet_CheckSockets(wiznet->sockset, 0) > 0) {
    for (int i = 0; i < MAX_WIZNET_SOCKETS; i++) {
      if (wiznet->tcpsock[i] != NULL && wiznet->tcpsock[i]->sock != NULL) {
        struct WizNetTCP* tcpsock = wiznet->tcpsock[i];
        while (!tcpsock->closed && tcpsock->len < WIZNET_BUFSIZE && SDLNet_SocketReady(tcpsock->sock)) {
          if (SDLNet_TCP_Recv(tcpsock->sock,&tcpsock->buf[tcpsock->len],1) > 0) {
             tcpsock->len++;
          } else {
             tcpsock->closed = true;
             break;
          }
          SDLNet_CheckSockets(wiznet->sockset, 0);
        }
      }
    }
  }

  uint32_t offset = value / 4;
  switch(ram[offset]) {
    case 0x10001: { // IP.StrToAdr + DNS.HostByName
      IPaddress addr;
      if (SDLNet_ResolveHost (&addr, (char*)(ram+offset+3), 0) != 0) {
          fprintf(stderr, "Host lookup %s failed: %s\n", (char*)(ram+offset+3), SDLNet_GetError());
          ram[offset+1] = 3601;
          ram[offset+2] = 0;
      } else {
          ram[offset+1] = 0;
          ram[offset+2] = SDL_SwapBE32(addr.host);
      }
      break;
    }
    case 0x10002: { // IP.AdrToStr
      int adr = SDL_SwapBE32(ram[offset+2]);
      sprintf((char*) (ram+offset+3), "%d.%d.%d.%d", (uint8_t)(adr), (uint8_t)(adr>>8), (uint8_t)(adr>>16), (uint8_t)(adr>>24));
      ram[offset+1] = 0;
      break;
    }
    case 0x10003: { // DNS.HostByNumber
      int adr = SDL_SwapBE32(ram[offset+2]);
      IPaddress addr;
      addr.host = adr;
      const char *host;
      if (!(host = SDLNet_ResolveIP(&addr))) {
        ram[offset+1] = 3601;
        sprintf((char*) (ram+offset+3), "%d.%d.%d.%d", (uint8_t)(adr), (uint8_t)(adr>>8), (uint8_t)(adr>>16), (uint8_t)(adr>>24));
      } else {
        ram[offset+1] = 0;
        strncpy((char*) (ram+offset+3), host, 128);
      }
      break;
    }
    case 0x10004: { // UDP.Open
      uint32_t lport = ram[offset+3];
      uint32_t socketid = 0;
      while (socketid < MAX_WIZNET_SOCKETS && wiznet->udpsock[socketid] != NULL)
        socketid++;
      ram[offset+2] = socketid;
      if (socketid == MAX_WIZNET_SOCKETS) {
        ram[offset+1] = 9999;
      } else {
        wiznet->udpsock[socketid] = SDLNet_UDP_Open((uint16_t)lport);
        if(!wiznet->udpsock[socketid]) {
          fprintf(stderr, "Opening UDP port on %d failed: %s\n", lport, SDLNet_GetError());
          ram[offset+1] = 9999;
        } else {
          IPaddress *address = SDLNet_UDP_GetPeerAddress(wiznet->udpsock[socketid], -1);
          ram[offset+3] = SDL_SwapBE16(address->port);
          ram[offset+1] = 0;
        }
      }
      break;
    }
    case 0x10005: { // UDP.Close
      uint32_t socketid = ram[offset+2];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->udpsock[socketid] != NULL) {
        SDLNet_UDP_Close(wiznet->udpsock[socketid]);
        wiznet->udpsock[socketid] = NULL;
        ram[offset+1] = 0;
      } else {
        ram[offset+1] = 3505;
      }
      break;
    }
    case 0x10006: { // UDP.Send
      uint32_t socketid = ram[offset+2], len = ram[offset+5];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->udpsock[socketid] != NULL) {
        UDPpacket *packet = SDLNet_AllocPacket(len);
        if(!packet) {
          fprintf(stderr, "Allocating UDP packet of size %d failed: %s\n", len, SDLNet_GetError());
          ram[offset+1] = 9999;
        } else {
          packet->len = len;
          memcpy(packet->data, (void*) &ram[offset+6], len);
          packet->address.host = SDL_SwapBE32(ram[offset+3]);
          packet->address.port = SDL_SwapBE16((uint16_t) ram[offset+4]);
          if (!SDLNet_UDP_Send(wiznet->udpsock[socketid], -1, packet)) {
            fprintf(stderr, "Sending UDP packet failed: %s\n", SDLNet_GetError());
            ram[offset+1] = 9999;
          } else {
            ram[offset+1] = 0;
          }
          SDLNet_FreePacket(packet);
        }
      } else {
        ram[offset+1] = 3505;
      }
      break;
    }
    case 0x10007: { // UDP.Receive
      uint32_t socketid = ram[offset+2], len = ram[offset+5];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->udpsock[socketid] != NULL) {
        UDPpacket *packet = SDLNet_AllocPacket(len);
        if(!packet) {
          fprintf(stderr, "Allocating UDP packet of size %d failed: %s\n", len, SDLNet_GetError());
          ram[offset+1] = 9999;
          ram[offset+5] = 0;
        } else {
          uint32_t wait_end = SDL_GetTicks() + ram[offset+6];
          int count;
          while (true) {
            count = SDLNet_UDP_Recv(wiznet->udpsock[socketid], packet);
            if (count != 0) break;
            int delay = wait_end - SDL_GetTicks();
            if (delay > 50) {
              SDL_Delay(50);
            } else if (delay > 0) {
              SDL_Delay(delay);
            } else {
              break;
            }
          }
          if (count == -1) { // error
            fprintf(stderr, "Receiving UDP packet failed: %s\n", SDLNet_GetError());
            ram[offset+1] = 9999;
            ram[offset+5] = 0;
          } else if (count == 0) { // no packet received -> timed out
            ram[offset+1] = 3704;
            ram[offset+5] = 0;
          } else { // packet received
            ram[offset+1] = 0;
            ram[offset+3] = SDL_SwapBE32(packet->address.host);
            ram[offset+4] = SDL_SwapBE16(packet->address.port);
            ram[offset+5] = packet->len;
            memcpy((void*)&ram[offset+7], packet->data, packet->len);
          }
          SDLNet_FreePacket(packet);
        }
      } else {
        ram[offset+1] = 3505;
        ram[offset+5] = 0;
      }
      break;
    }
    case 0x10008: { // TCP.Open
      uint32_t lport = ram[offset+3];
      uint32_t fip = SDL_SwapBE32(ram[offset+4]);
      uint32_t fport = ram[offset+5];
      if (fip == 0 && fport == 0) { // listen
        uint32_t socketid = 0;
        while (socketid < MAX_WIZNET_SOCKETS && wiznet->listener[socketid] != NULL)
          socketid++;
        ram[offset+2] = socketid + MAX_WIZNET_SOCKETS*2;
        if (socketid == MAX_WIZNET_SOCKETS) {
          ram[offset+1] = 3706;
        } else {
          IPaddress ip;
          if(SDLNet_ResolveHost(&ip, NULL, (uint16_t)lport) == 0) {
             wiznet->listener[socketid] = SDLNet_TCP_Open(&ip);
          }
          if(!wiznet->listener[socketid]) {
            fprintf(stderr, "Opening TCP listener on %d failed: %s\n", lport, SDLNet_GetError());
            ram[offset+1] = 3705;
          } else {
            ram[offset+1] = 0;
          }
        }
      } else { // connect
        uint32_t socketid = 0;
        while (socketid < MAX_WIZNET_SOCKETS && wiznet->tcpsock[socketid] != NULL)
          socketid++;
        ram[offset+2] = socketid;
        if (socketid == MAX_WIZNET_SOCKETS) {
          ram[offset+1] = 3706;
        } else {
          struct WizNetTCP *sock = calloc(1, sizeof(*sock));
          wiznet->tcpsock[socketid] = sock;
          IPaddress ip;
          ip.host = fip;
          ip.port = SDL_SwapBE16((uint16_t)fport);
          sock->sock=SDLNet_TCP_Open(&ip);
          if(!sock->sock) {
            fprintf(stderr, "Opening TCP socket to host %d.%d.%d.%d port %d failed: %s\n", (uint8_t)(fip), (uint8_t)(fip>>8), (uint8_t)(fip>>16), (uint8_t)(fip>>24), fport, SDLNet_GetError());
            ram[offset+1] = 3701;
          } else if (SDLNet_TCP_AddSocket(wiznet->sockset,sock->sock) == -1) {
             fprintf(stderr, "Adding socket to socket set failed: %s\n", SDLNet_GetError());
             ram[offset+1] = 3702;
          } else {
            ram[offset+1] = 0;
          }
        }
      }
      break;
    }
    case 0x10009: { // TCP.SendChunk
      uint32_t socketid = ram[offset+2];
      uint32_t len = ram[offset+3];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->tcpsock[socketid] != NULL) {
        if (SDLNet_TCP_Send(wiznet->tcpsock[socketid]->sock, (void*) &ram[offset+5], len) < (int)len) {
          fprintf(stderr, "Sending %d bytes via TCP failed: %s\n", len, SDLNet_GetError());
          ram[offset+1] = 3702;
        } else {
          ram[offset+1] = 0;
        }
      } else {
        ram[offset+1] = 3706;
      }
      break;
    }
    case 0x1000A: { // TCP.ReceiveChunk
      uint32_t socketid = ram[offset+2];
      uint32_t len = ram[offset+3];
      uint32_t minlen = ram[offset+4];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->tcpsock[socketid] != NULL) {
        if (wiznet->tcpsock[socketid]->len == 0 && wiznet->tcpsock[socketid]->closed) {
          ram[offset+1] = 3707;
          ram[offset+3] = 0;
        } else if (wiznet->tcpsock[socketid]->len < minlen) {
          ram[offset+1] = 3704;
          ram[offset+3] = 0;
        } else {
          if (len > wiznet->tcpsock[socketid]->len) {
            len = wiznet->tcpsock[socketid]->len;
          }
          minlen = wiznet->tcpsock[socketid]->len-len;
          ram[offset+1] = 0;
          ram[offset+3] = len;
          memcpy((void*)&ram[offset+5], wiznet->tcpsock[socketid]->buf, len);
          memcpy(wiznet->tcpsock[socketid]->buf, &wiznet->tcpsock[socketid]->buf[len], minlen);
          wiznet->tcpsock[socketid]->len = minlen;
        }
      } else {
        ram[offset+1] = 3706;
        ram[offset+3] = 0;
      }
      break;
    }
    case 0x1000B: { // TCP.Available
      uint32_t socketid = ram[offset+2];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->tcpsock[socketid] != NULL) {
        ram[offset+1] = wiznet->tcpsock[socketid]->len + (wiznet->tcpsock[socketid]->closed ? 1 : 0);
      } else {
        ram[offset+1] = 0;
      }
      break;
    }
    case 0x1000C: { // TCP.Close
      uint32_t socketid = ram[offset+2];
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->tcpsock[socketid] != NULL) {
        if (SDLNet_TCP_DelSocket(wiznet->sockset, wiznet->tcpsock[socketid]->sock) == -1) {
          fprintf(stderr, "Removing socket from socket set failed: %s\n", SDLNet_GetError());
        }
        SDLNet_TCP_Close(wiznet->tcpsock[socketid]->sock);
        free(wiznet->tcpsock[socketid]);
        wiznet->tcpsock[socketid] = NULL;
        ram[offset+1] = 0;
      } else if (socketid >= MAX_WIZNET_SOCKETS * 2 && socketid < MAX_WIZNET_SOCKETS * 3 && wiznet->listener[socketid-MAX_WIZNET_SOCKETS*2] != NULL) {
        SDLNet_TCP_Close(wiznet->listener[socketid-MAX_WIZNET_SOCKETS*2]);
        wiznet->listener[socketid-MAX_WIZNET_SOCKETS*2] = NULL;
        ram[offset+1] = 0;
      } else {
        ram[offset+1] = 3706;
      }
      break;
    }
    case 0x1000D: { // TCP.Accept
      uint32_t socketid = ram[offset+2] - MAX_WIZNET_SOCKETS*2;
      if (socketid < MAX_WIZNET_SOCKETS && wiznet->listener[socketid] != NULL) {
        uint32_t clientid = 0;
        while (clientid < MAX_WIZNET_SOCKETS && wiznet->tcpsock[clientid] != NULL)
          clientid++;
        ram[offset+1] = 0;
        if (clientid == MAX_WIZNET_SOCKETS) {
          ram[offset+3] = -1;
        } else {
          TCPsocket clientsock = SDLNet_TCP_Accept(wiznet->listener[socketid]);
          if (clientsock == NULL) {
            ram[offset+3] = -1;
          } else if (SDLNet_TCP_AddSocket(wiznet->sockset,clientsock) == -1) {
             fprintf(stderr, "Adding accepted socket to socket set failed: %s\n", SDLNet_GetError());
             ram[offset+3] = -1;
          } else {
            struct WizNetTCP *sock = calloc(1, sizeof(*sock));
            wiznet->tcpsock[clientid] = sock;
            sock->sock=clientsock;
            IPaddress *remote_ip = SDLNet_TCP_GetPeerAddress(clientsock);
            ram[offset+3] = clientid;
            if(!remote_ip) {
              printf("Obtain remote IP failed: %s\n", SDLNet_GetError());
              ram[offset+4] = 0;
              ram[offset+5] = 0;
            }
            else {
              ram[offset+4] = SDL_SwapBE32(remote_ip->host);
              ram[offset+5] = SDL_SwapBE16(remote_ip->port);
            }
          }
        }
      } else {
        ram[offset+1] = 3706;
        ram[offset+3] = 0;
      }
      break;
    }
  }
}

static void fail(int code, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(code);
}

struct RISC_WizNet *wiznet_new() {
  if (SDLNet_Init() != 0) {
    fail(1, "Unable to initialize SDLNet: %s", SDLNet_GetError());
  }
  struct WizNet *wiznet = calloc(1, sizeof(*wiznet));
  wiznet->wiznet = (struct RISC_WizNet) {
    .write = wiznet_write
  };
  wiznet->sockset = SDLNet_AllocSocketSet(MAX_WIZNET_SOCKETS);
  if(!wiznet->sockset) {
    fail(1, "Unable to allocate socket set: %s\n", SDLNet_GetError());
  }
  return &wiznet->wiznet;
}
