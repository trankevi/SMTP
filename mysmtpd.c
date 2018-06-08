#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024
#define MAX_ADDRESS_LENGTH 50

static void handle_client(int fd);

int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
    return 1;
  }
  
  run_server(argv[1], handle_client);
  
  return 0;
}

void handle_client(int fd) {
  
  struct utsname unameData;
  uname(&unameData);
  send_string(fd, "220 %s Simple mail transfer service ready \r\n", unameData.nodename);

  net_buffer_t nb = nb_create(fd, MAX_LINE_LENGTH);
  char out[MAX_LINE_LENGTH];
  int linebytes = nb_read_line(nb, out); 

  int helo = 0;
  int mail = 0;
  int rcpt = 0;

  char fromaddress[MAX_ADDRESS_LENGTH];
  char toaddress[MAX_ADDRESS_LENGTH];

  user_list_t users = create_user_list();

  while (linebytes != 0){

    //replace \r,\n\,or space with \0. save so we can restore later
    char fourthchar = out[4];
    out[4] = '\0';

   
    if (strcasecmp(out, "HELO") == 0){
      if (fourthchar == '\r' ||
	  out[5] == '\r'){
	send_string(fd, "501 HELO parameter error. HELO entered without identifier \r\n");
      }
      else{
	out[4] = fourthchar;
	helo = 1;
	send_string(fd, "250 OK this is %s, %s", unameData.nodename, out);
      }
    }


    else if (strcasecmp(out, "MAIL") == 0 &&
	     fourthchar == ' '){
      if (helo == 0) {
	send_string(fd, "503 Bad sequence of commands. MAIL requires HELO first \r\n");
      }
      
      else {
	out[4] = fourthchar;

	//check for "MAIL FROM:<u>" argument format here                                                
	char userstart = out[11];
	out[11] = '\0';
	int missingfrom = strcasecmp(out, "MAIL FROM:<");
	out[11] = userstart;
	int outlen = strlen(out);
	int userend = outlen - 3;
	char endchar = out[userend];
	
	if (missingfrom != 0){
	  send_string(fd, "501 MAIL syntax error. Format is MAIL FROM:<user> \r\n");
	}
	else if (endchar != '>'){
	  send_string(fd, "501 MAIL syntax error. Missing '>' in format MAIL FROM:<user> \r\n");
	}
	
	else {
	  //get address for from
	  char *start;
	  char *end;
	  int addresslen;
	  start = strchr(out, '<');
	  end = strrchr(out, '>');
	  start++;
	  addresslen = end - start;
	  memset(fromaddress, 0, sizeof fromaddress);
	  strncpy(fromaddress, start, addresslen);
	  
	  //email cannot contain: <>"():\' and space
	  const char *invalid = "<>\"():\\' ";
	  size_t len = strlen(fromaddress);
	  size_t spn = strcspn(fromaddress, invalid);
	  
	  if (spn != len){
	    send_string(fd, "555 from parameters not recognized (invalid email): %s \r\n", fromaddress);
	  }
	  else {                                                                                        
	    send_string(fd, "250 OK message initialization. From: %s \r\n", fromaddress);               
	    mail = 1;                                                                                   
	  }   
	}
      }
    }


    else if (strcasecmp(out, "RCPT") == 0 &&
	     fourthchar == ' '){
      if (mail == 1) {
	out[4] = fourthchar;     
       
	//check for "RCPT TO:<u>" here                                                                       
	char userstart = out[9];
	out[9] = '\0';
	int missingto = strcasecmp(out, "RCPT TO:<");
	out[9] = userstart;
	int outlen = strlen(out);
	int userend = outlen - 3;
	char endchar = out[userend];

	if (missingto != 0){
	  send_string(fd, "501 RCPT syntax error. Format is RCPT TO:<user> \r\n");
	}
	else if (endchar != '>'){
	  send_string(fd, "501 RCPT syntax error. Missing '>' in format RCPT TO:<user> \r\n");
	}
	else {
	  //get address for to
	  char *start;
	  char *end;
	  int addresslen;
	  start = strchr(out, '<');
	  end = strrchr(out, '>');
	  start++;
	  addresslen = end - start;
	  memset(toaddress, 0, sizeof toaddress);
	  strncpy(toaddress, start, addresslen);

	  //verify that it matches an email in users.txt
	  if (is_valid_user(toaddress, NULL) != 0) {
	    send_string(fd, "250 OK recipient specified. To: %s \r\n", toaddress);
	    add_user_to_list(&users, toaddress);
	    rcpt = 1;
	  }
	  else {
	    send_string(fd, "550 no such user. To: %s \r\n", toaddress); 
	  }
	}
      }
      else {
	send_string(fd, "503 Bad sequence of commands. RCPT requires MAIL first \r\n");
      }
    }


    else if (strcasecmp(out, "DATA") == 0 &&
             fourthchar == '\r'){
      // check mail, rcpt
      if (mail == 0 || rcpt == 0){
	send_string(fd, "503 Bad sequence of commands. DATA requires MAIL and RCPT first \r\n");
      }
      else {
	send_string(fd, "354 OK reading message contents \r\n");
	
	// HANDLE MESSAGE CONTENTS
	static char template[] = "myfile-XXXXXX";
	char fname[50];
	strcpy(fname, template);
	int filedes;
	filedes = mkstemp(fname);
	int reading = 1;
	
	while (reading == 1){
	  linebytes = nb_read_line(nb, out);
	  char firstchar = out[1];
	  out[1] = '\0';
	  if (strcasecmp(out, ".") == 0 &&
	      firstchar == '\r' &&
	      out[2] == '\n'){
	    reading = 0;
	  }

	  out[1] = firstchar;	  
	  write(filedes, out, strlen(out));
	}
	
	mail = 0;
	rcpt = 0;
	save_user_mail(fname, users);
	destroy_user_list(users);
	close(filedes);
	unlink(fname);
	send_string(fd, "250 OK got message contents and mail queued for sending \r\n");      
      }
    }


    else if (strcasecmp(out, "NOOP") == 0 &&
             fourthchar == '\r'){
      send_string(fd, "250 OK noop \r\n");
    }


    else if (strcasecmp(out, "QUIT") == 0 &&
             fourthchar == '\r'){
      send_string(fd, "221 OK quit received. Goodbye. \r\n");
      break;
    }


    else if ((strcasecmp(out, "EHLO") == 0 ||
	      strcasecmp(out, "RSET") == 0 ||
	      strcasecmp(out, "VRFY") == 0 ||
	      strcasecmp(out, "EXPN") == 0 ||
	      strcasecmp(out, "HELP") == 0) &&
	     fourthchar == '\r'){
      send_string(fd, "502 %s is an unsupported command \r\n", out);
    }


    else {
      out[4] = fourthchar;
      send_string(fd, "500 following command not recognized: %s", out);
    }                                                         
    
    //then read next line
    linebytes = nb_read_line(nb, out);
  }

  //when QUIT received, gets here by break
  nb_destroy(nb);
  close(fd);
}
