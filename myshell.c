#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include "parser.h"
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

int over;	// over=1=> Estoy en medio de la ejecución de comandos.   over=0=>estoy esperando comandos (inhabilito señales)
int *pids;	//índice de procesos
int n;		//número de comandos a ejecutar

void manejadorSig(int signum){
	if(signum==SIGINT ||signum==SIGQUIT){	//Si las señales recibidas son SIGINT o SIGQUIT
		if(over==0)							//Si estoy esperando comandos, ignoro señal
			signal(signum,SIG_IGN);
		else								//Si estoy ejecutando procesos, aplico señal
			for(int i=0;i<n;i++)
				kill(pids[i],signum);
	}
}

int checkCommandsCD(tline *line, int n){
	int aux=0;	//cd no ejecutado
	for (int i=0;i<n;i++){
		if(strcmp(line->commands[0].argv[0],"cd")==0){
			if(n==1){
				if(line->commands[0].argc==1){	//CD a /home
					char *str=getenv("HOME");
					int val=chdir(str);
					if(val!=0)	//CD fallido
						printf("msh: cd: $HOME: No existe el archivo o el directorio\n");
				}
				else{
					char *dircd=line->commands[0].argv[1];	//cd a directorio
					if(line->commands[0].argc==2){	//cd to dir
						int val=chdir(dircd);
						if(val!=0)
							printf("msh: cd: %s: No existe el archivo o el directorio\n",dircd);
					}
					else{ printf("msh: cd: %s: demasiados argumentos\n",dircd);}
				}
				aux=1;	//cd ejecutado correctamente
			}
			else
				aux=-1;	//no se puede ejecutar el cd por exceso de argumentos
		}
	}
	return aux;
}

int main(void) {
	char buf[1024];
	tline * line;
    pid_t pid;
	extern int errno;
	int redentrada,redsalida;
	signal(SIGINT,manejadorSig);
	char dire[PATH_MAX];
	printf("%s/msh> ",getcwd(dire, sizeof(dire)));

	while (fgets(buf, 1024, stdin)) {
		signal(SIGINT,manejadorSig);
		line = tokenize(buf);	//parseo entrada de texto
		over=1;					//aviso que estoy ejecutando procesos
		int k;					//iterar entre "komandos"
       	n=line->ncommands;  	//número de mandatos

		int auxCD=checkCommandsCD(line,n);
		if(auxCD==0 || auxCD==1){						//compruebo si hay llamada a cd y si está bien llamada
			int **p=(int**)malloc((n-1)*sizeof(int*));	//inicializo array de descriptores (n*2)
			for(int k=0;k<n-1;k++){
				p[k]=(int *)malloc(2*sizeof(int));
			}
			pids= (int*)malloc((n-1)*sizeof(int)); 		//inicializo array de pids
			for(k=0;k<n-1;k++){							//creo las n-1 pipes
				pipe(p[k]);
			}
		
			for(k=0;k<n;k++){ 	// recorro mandatos
				pid = fork(); 	//creo hijo
				if(pid<0){
					printf("Error number %d\n",errno);
					perror("Program");
				}
				else if(pid == 0){ //Es el hijo
					if(k==0){	//si es el primer comando a ejecutar
						if(line->redirect_input!=NULL){		//compruebo si redirecciono entrada a fichero
							redentrada = open(line->redirect_input,O_RDONLY,0600);
							if(redentrada!=-1){
								dup2(redentrada,0);
							}else{
								printf("Error number %d\n",errno);
								perror("Program");
								exit(1);
							}
						}
						if(n!=1){	//si no es el único comando, del primer pipe cierro su entrada y redirecciono su salida
							dup2(p[0][1],STDOUT_FILENO);
							close(p[0][0]);
						}else{		//compruebo si redirecciono salida a fichero
							if(line->redirect_output!= NULL){
								redsalida = open(line->redirect_output,O_WRONLY|O_CREAT|O_TRUNC,0600);
								if(redsalida!=-1){
									dup2(redsalida,1);
								}else{
									printf("Error number %d\n",errno);
									perror("Program");
									exit(1);
								}
							}
						}
						for(int w=k+1;w<n-1;w++){	//cierro el resto de pipes
							close(p[w][0]);
							close(p[w][1]);
						}
					}
					else if(k==n-1){	//último comando
						dup2(p[k-1][0],STDIN_FILENO);	//redirecciono su entrada de pipe
						if(line->redirect_output){		//compruebo si redirecciono salida a fichero
							redsalida = open(line->redirect_output,O_WRONLY|O_CREAT|O_TRUNC,0600);
							if (redsalida!=-1){
								dup2(redsalida,STDOUT_FILENO);
							}else{
								printf("Error number %d\n",errno);
								perror("Program");
								exit(1);
							}
						}
						for(int w=0;w<k-1;w++){	//cierro todas las pipes menos la última
							close(p[w][0]);
							close(p[w][1]);
						}
						close(p[k-1][1]);		//cierro la salida de su pipe
					}
					else{	//mandato intermedio
						dup2(p[k-1][0],STDIN_FILENO);	//redirijo entrada
						dup2(p[k][1],STDOUT_FILENO);	//redirijo salida
						for(int w=0;w<n-1;w++){			//cierro todas las tuberias menos por donde leo y escribo.
							if(w!=k-1){
								close(p[w][0]);
							}
							if(w!=k){
								close(p[w][1]);
							}
						}
					}
					execvp(line->commands[k].filename,line->commands[k].argv);	//ejecuto comando
					exit(1); 
				}else	//es el padre
					pids[k]=pid;						
			}	
			char wia[PATH_MAX];
			printf("%s/msh> ",getcwd(wia, sizeof(wia)));  	//prompt
			for(int w=0;w<n-1;w++){							//cierro todas las tuberías.
				close(p[w][0]);
				close(p[w][1]);
			}
			for(int w=0;w<n;w++){							//espero a todos los hijos
				waitpid(pids[w],NULL,0);
			} 

			over=0;

			for(int w=0;w<n-1;w++){							//libero memoria
				free(p[w]);
			}
			free(p);
		}
		else{
			char wia[PATH_MAX];								//prompt
			printf("%s/msh> ",getcwd(wia, sizeof(wia)));  
		}
	}
	return 0;
}