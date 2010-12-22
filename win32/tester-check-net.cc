// Copyright (C) 2010 Timothy Brownawell <tbrownaw@prjek.net>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// threads
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// sockets
#include <WinSock2.h>
#include <WS2tcpip.h>

#include <stdio.h>

DWORD WINAPI timer(void *)
{
    Sleep(5000);
    ExitProcess(5);
}

int main(void)
{
  char const myname[] = "check_net";
  CreateThread(0, 0, &timer, 0, 0, 0);

  WSADATA wsa_data;
  int started = WSAStartup(MAKEWORD(2,2), &wsa_data);
  if (started != 0)
  {
    fprintf(stderr, "%s: failed to init Winsock: %d\n", myname, started);
    ExitProcess(1);
  }
  bool use_ip6 = false;
  
  SOCKET listen_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
  if (listen_sock == INVALID_SOCKET)
  {
    use_ip6 = true;
    SOCKET listen_sock = WSASocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
    if (listen_sock == INVALID_SOCKET)
        ExitProcess(1);
  }

  struct sockaddr_in my_addr_4;
  struct sockaddr_in6 my_addr_6;
  struct sockaddr *my_addr;
  int my_addr_size;
  if (use_ip6)
  {
      my_addr_6.sin6_family = AF_INET6;

      my_addr = (struct sockaddr *) &my_addr_6;
      my_addr_size = sizeof(my_addr_6);
      
      if (WSAStringToAddressA("[::1]:21845", AF_INET6, 0, my_addr, &my_addr_size) != 0)
      {
          fprintf(stderr, "%s: could not parse PIv6 address: %d\n", myname, WSAGetLastError());
          ExitProcess(1);
      }
  }
  else
  {
      my_addr_4.sin_family = AF_INET;

      my_addr = (struct sockaddr *) &my_addr_4;
      my_addr_size = sizeof(my_addr_4);
      
      if (WSAStringToAddressA("127.0.0.1:21845", AF_INET, 0, my_addr, &my_addr_size) != 0)
      {
          fprintf(stderr, "%s: could not parse PIv4 address: %d\n", myname, WSAGetLastError());
          ExitProcess(1);
      }
  }

  if (bind(listen_sock, my_addr, my_addr_size) != 0)
      ExitProcess(1);
  if (listen(listen_sock, 1) != 0)
      ExitProcess(1);
  
  SOCKET client_sock =  WSASocket(use_ip6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
  if (WSAConnect(client_sock, my_addr, my_addr_size, 0, 0, 0, 0) != 0)
  {
      fprintf(stderr, "%s: WSAConnect: %d\n", myname, WSAGetLastError());
      ExitProcess(1);
  }
  SOCKET server_sock = WSAAccept(listen_sock, 0, 0, 0, 0);
  if (server_sock == INVALID_SOCKET)
      ExitProcess(1);

  WSABUF buffer;
  buffer.buf = "abc123";
  buffer.len = 7;
  DWORD amount_sent;
  WSAOVERLAPPED overlapped;
  overlapped.hEvent = WSACreateEvent();
  int sent = WSASend(client_sock, &buffer, 1, &amount_sent, 0, &overlapped, 0);
  if (sent != 0 && WSAGetLastError() != WSA_IO_PENDING)
      ExitProcess(1);
  
  WSABUF rbuffer;
  char foo[8] = {0, 0, 0, 0, 0, 0, 1, 0};
  rbuffer.buf = foo;
  rbuffer.len = 8;
  DWORD amount_rcvd;
  WSAOVERLAPPED roverlapped;
  roverlapped.hEvent = WSACreateEvent();
  DWORD rcvflags = 0;
  int rcvd = WSARecv(server_sock, &rbuffer, 1, &amount_rcvd, &rcvflags, &roverlapped, 0);
  if (rcvd != 0)
  {
      int er = WSAGetLastError();
      if (er != WSA_IO_PENDING)
      {
        fprintf(stderr, "%s: WSARecv: %d\n", myname, er);
        ExitProcess(1);
      }
  }
  
  if (sent != 0)
      if (WSAWaitForMultipleEvents(1, &overlapped.hEvent, FALSE, WSA_INFINITE, FALSE) == WSA_WAIT_FAILED)
          ExitProcess(1);
  if (rcvd != 0)
      if (WSAWaitForMultipleEvents(1, &roverlapped.hEvent, FALSE, WSA_INFINITE, FALSE) == WSA_WAIT_FAILED)
          ExitProcess(1);

  if (strcmp("abc123", rbuffer.buf) != 0)
      ExitProcess(1);

  return 0;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
