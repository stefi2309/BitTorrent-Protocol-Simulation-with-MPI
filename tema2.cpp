#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

#define INIT 0
#define FINISHED 1
#define UPDATE 2
#define SWARM_REQ 3
#define SEG_REQ 4
#define HASH 5

// var globale pentru fisiere
int nr_total, nr_owned, nr_wanted;

// struct pt un swarm
struct Swarm {
  int *seg_id;  // segmentele detinute de client
  int client;   // rank client
};

// struct pt un segment
struct Segment {
  char hash[HASH_SIZE];  // hash segment
  bool owned;            // indicator daca este detinut
};

struct File {
  Swarm *swarm;             // info despre swarm
  int swarm_idx;            // indexul curent in swarm
  Segment *seg;             // vector de segmente
  int total_seg;            // total segmente
  int owned_seg;            // nr segmente detinute
  char name[MAX_FILENAME];  // numele fisier
};

// vector global de fisiere
File *files;

// citire segmente din input
void read_segments(FILE *file, File *file_data) {
  fscanf(file, "%s %d\n", file_data->name, &file_data->total_seg);
  file_data->owned_seg = file_data->total_seg;
  file_data->seg = (Segment *)calloc(file_data->total_seg, sizeof(Segment));
  file_data->swarm = NULL;

  for (int i = 0; i < file_data->total_seg; i++) {
    fread(file_data->seg[i].hash, HASH_SIZE, sizeof(char), file);
    fgetc(file);  // scoatem newline sau spatiu
    file_data->seg[i].owned = true;
  }
}

// citire input specific unui rank
void read(int rank) {
  char name[MAX_FILENAME];
  sprintf(name, "in%d.txt", rank);

  FILE *file = fopen(name, "r");
  if (file == NULL) {
    printf("Eroare la citirea fisierului");
    exit(1);
  }

  fscanf(file, "%d\n", &nr_owned);
  files = (File *)calloc(nr_owned, sizeof(File));

  for (int i = 0; i < nr_owned; i++) {
    read_segments(file, &files[i]);
  }

  fscanf(file, "%d", &nr_wanted);

  nr_total = nr_owned + nr_wanted;
  files = (File *)realloc(files, sizeof(File) * (nr_total));

  for (int i = nr_owned; i < nr_total; i++) {
    fscanf(file, "%s", files[i].name);
  }
  fclose(file);
}

// scriere fisier dupa ce a fost descarcat complet
void write(int rank, int idx) {
  std::string output_name =
      "client" + std::to_string(rank) + "_" + files[idx].name;
  std::ofstream out_file(output_name);

  if (!out_file) {
    std::cerr << "Error opening output file: " << output_name << std::endl;
    return;
  }

  for (int i = 0; i < files[idx].total_seg; i++) {
    out_file.write(files[idx].seg[i].hash, HASH_SIZE);
    out_file.put('\n');
  }
  nr_owned++;
  nr_wanted--;
  out_file.close();
}

// eliberare memorie swarm
void free_swarm(Swarm *swarm, int swarm_idx) {
  if (swarm) {
    for (int i = 0; i < swarm_idx; i++) {
      free(swarm[i].seg_id);
    }
  }
  free(swarm);
}

// eliberare memorie toate fisierele
void free_data(File *files, int nr) {
  for (int i = 0; i < nr; i++) {
    free_swarm(files[i].swarm, files[i].swarm_idx);

    if (files[i].owned_seg) {
      free(files[i].seg);
    }
  }
  free(files);
}

// caut primul segment care lipseste si returnez indexul
int find_seg(int idx) {
  for (int i = 0; i < files[idx].total_seg; i++) {
    if (!files[idx].seg[i].owned) {
      return i;
    }
  }
  return -1;
}

// selectarea unui seed pentru un segment specific
int seed_for_seg(int idx, int seg_idx) {
  std::vector<int> seeds;
  for (int i = 0; i < files[idx].swarm_idx; i++) {
    if (files[idx].swarm[i].seg_id[seg_idx]) {
      // adaug clientul care detine segmentul
      seeds.push_back(files[idx].swarm[i].client);
    }
  }

  if (seeds.empty()) {
    return -1;
  }

  // aleg un seed aleatoriu din lista
  int rand_idx = rand() % seeds.size();
  return seeds[rand_idx];
}

// trimitere cerere de swarm pt un fisier
void send_swarm_req(int idx, File *files) {
  MPI_Send(files[idx].name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, SWARM_REQ,
           MPI_COMM_WORLD);
}

// primire info despre swarm pt un fisier
void recv_file_info(int idx, File *files) {
  MPI_Status status;
  MPI_Recv(&files[idx].swarm_idx, 1, MPI_INT, TRACKER_RANK, SWARM_REQ,
           MPI_COMM_WORLD, &status);
  MPI_Recv(&files[idx].total_seg, 1, MPI_INT, TRACKER_RANK, SWARM_REQ,
           MPI_COMM_WORLD, &status);
}

// alocare memorie pt segmente
void alloc_seg(int idx, File *files) {
  files[idx].seg = (Segment *)calloc(files[idx].total_seg, sizeof(Segment));
  for (int i = 0; i < files[idx].total_seg; i++) {
    files[idx].seg[i].owned = false;
  }
  files[idx].owned_seg = 0;
}

// alocare memorie pt swarm
void alloc_swarm(int idx, File *files) {
  files[idx].swarm = (Swarm *)calloc(files[idx].swarm_idx, sizeof(Swarm));
}

// primeste info despre swarm de la tracker
void recv_swarm_info(int idx, File *files) {
  MPI_Status status;
  for (int i = 0; i < files[idx].swarm_idx; i++) {
    MPI_Recv(&files[idx].swarm[i].client, 1, MPI_INT, TRACKER_RANK, SWARM_REQ,
             MPI_COMM_WORLD, &status);

    files[idx].swarm[i].seg_id =
        (int *)calloc(files[idx].total_seg, sizeof(int));
    MPI_Recv(files[idx].swarm[i].seg_id, files[idx].total_seg, MPI_INT,
             TRACKER_RANK, SWARM_REQ, MPI_COMM_WORLD, &status);
  }
}

// cere info despre swarm pt fiecare fisier
void req_swarm(int rank, int nr_owned, int nr_total, File *files) {
  for (int i = nr_owned; i < nr_total; i++) {
    send_swarm_req(i, files);
    recv_file_info(i, files);
    alloc_seg(i, files);
    alloc_swarm(i, files);
    recv_swarm_info(i, files);
  }
}

// primeste info despre un segment de la un peer
void recv_seg_info(int rank, int idx, int seg_idx, File *files) {
  MPI_Status status;
  MPI_Recv(files[idx].seg[seg_idx].hash, HASH_SIZE, MPI_CHAR, rank, HASH,
           MPI_COMM_WORLD, &status);
  files[idx].seg[seg_idx].owned = true;
  files[idx].owned_seg++;
}

// trimite cereri de segmente la un peer specific
void send_seg_req(int rank, int idx, int seg_idx, File *files) {
  MPI_Send(files[idx].name, MAX_FILENAME, MPI_CHAR, rank, SEG_REQ,
           MPI_COMM_WORLD);
  MPI_Send(&seg_idx, 1, MPI_INT, rank, SEG_REQ, MPI_COMM_WORLD);
}

// cere un segment anume de la un seed si primeste info despre el
void req_seg(int rank, int idx, int seg_idx) {
  send_seg_req(rank, idx, seg_idx, files);
  recv_seg_info(rank, idx, seg_idx, files);
}

// trimite info actualizate la tracker despre fisiere
void send_tracker(int tag, int nr) {
  MPI_Send(&nr, 1, MPI_INT, TRACKER_RANK, tag, MPI_COMM_WORLD);
  for (int i = 0; i < nr; i++) {
    MPI_Send(files[i].name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, tag,
             MPI_COMM_WORLD);
    MPI_Send(&files[i].owned_seg, 1, MPI_INT, TRACKER_RANK, tag,
             MPI_COMM_WORLD);
    for (int j = 0; j < files[i].owned_seg; j++) {
      MPI_Send(files[i].seg[j].hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, tag,
               MPI_COMM_WORLD);
    }
  }
}

// trimite info despre swarm pt un fisier solicitat de un client
void send_swarm(int rank, File *files, int total_files, const char *req_file) {
  bool ok = true;
  for (int i = 0; i < total_files && ok; i++) {
    // verifica daca numele corespunde celui solicitat
    if (strncmp(files[i].name, req_file, MAX_FILENAME) == 0) {
      MPI_Send(&files[i].swarm_idx, 1, MPI_INT, rank, SWARM_REQ,
               MPI_COMM_WORLD);
      MPI_Send(&files[i].total_seg, 1, MPI_INT, rank, SWARM_REQ,
               MPI_COMM_WORLD);

      // trimite detalii despre fiecare client din swarm si ce segmente detine
      for (int j = 0; j < files[i].swarm_idx; j++) {
        MPI_Send(&files[i].swarm[j].client, 1, MPI_INT, rank, SWARM_REQ,
                 MPI_COMM_WORLD);

        if (files[i].swarm[j].seg_id) {
          MPI_Send(files[i].swarm[j].seg_id, files[i].total_seg, MPI_INT, rank,
                   SWARM_REQ, MPI_COMM_WORLD);
        } else {
          // trimite un vector gol daca nu exista info despre segmente
          int *empty = (int *)calloc(files[i].total_seg, sizeof(int));
          MPI_Send(empty, files[i].total_seg, MPI_INT, rank, SWARM_REQ,
                   MPI_COMM_WORLD);
          free(empty);
        }
      }
      ok = false;  // opreste bucla dupa ce info au fost trimise
    }
  }
}

// trimite mesaj la o anumita dest cu un anumit tag
void send_msg(const std::string &msg, int dest, int tag) {
  MPI_Send(msg.c_str(), MAX_FILENAME, MPI_CHAR, dest, tag, MPI_COMM_WORLD);
}

void *download_thread_func(void *arg) {
  MPI_Status status;
  int rank = *(int *)arg;

  // initiaza comunicarea cu tracker ul pt a raporta fisierele detinute
  send_tracker(INIT, nr_owned);

  // asteapta confirmare de la tracker
  char ok_msg[2];
  MPI_Recv(ok_msg, 2, MPI_CHAR, TRACKER_RANK, INIT, MPI_COMM_WORLD, &status);

  // cere info despre swarm pt fiecare fisier dorit
  req_swarm(rank, nr_owned, nr_total, files);

  // bucla pana cand toate fisierele dorite sunt descarcate
  while (nr_owned < nr_total) {
    int nr = 10;  // nr max de segmente de procesat
    bool ok = true;
    while (nr && ok) {
      // cauta primul segment lipsa
      int seg_idx = find_seg(nr_owned);
      if (seg_idx != -1) {
        // obtine un seed de la care sa descarce segmentul
        int seed = seed_for_seg(nr_owned, seg_idx);
        // cere segmentul de la seed
        req_seg(seed, nr_owned, seg_idx);
        nr--;
      } else {
        // Scrie fisierul complet
        write(rank, nr_owned);
        if (nr_owned == nr_total) {
          ok = false;
        }
      }
    }
    // spune tracker ului starea actuala
    send_msg("UPDATE", TRACKER_RANK, UPDATE);
    // trimite info actualizate despre fisierele detinute
    if (nr_wanted == 0) {
      send_tracker(UPDATE, nr_owned);
    } else {
      send_tracker(UPDATE, nr_owned + 1);
    }
    // asteapta confirmarea de la tracker
    MPI_Recv(ok_msg, 2, MPI_CHAR, TRACKER_RANK, UPDATE, MPI_COMM_WORLD,
             &status);
  }
  // anunta tracker ul ca procesul s a terminat
  send_msg("FINISHED", TRACKER_RANK, FINISHED);
  return NULL;
}

void *upload_thread_func(void *arg) {
  int rank = *(int *)arg;
  MPI_Status status;

  // bucla pana cand primeste semnalul de oprire
  while (true) {
    char req_name[MAX_FILENAME];
    // primeste numele pt care este solicitat un segment
    MPI_Recv(req_name, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, SEG_REQ,
             MPI_COMM_WORLD, &status);
    int source = status.MPI_SOURCE;
    // verifica daca este un semnal de oprire de la tracker
    if (source == TRACKER_RANK && strcmp(req_name, "STOP") == 0) {
      break;
    }

    // primeste indexul segmentului solicitat
    int req_seg_idx;
    MPI_Recv(&req_seg_idx, 1, MPI_INT, source, SEG_REQ, MPI_COMM_WORLD,
             &status);
    bool ok = true;
    // cauta fisierul respectiv si trimite segmentul solicitat
    for (int i = 0; i < nr_owned && ok; i++) {
      if (strcmp(files[i].name, req_name) == 0) {
        MPI_Send(files[i].seg[req_seg_idx].hash, HASH_SIZE, MPI_CHAR, source,
                 HASH, MPI_COMM_WORLD);
        ok = false;
      }
    }
  }

  return NULL;
}

// primeste info despre un fisier de la un client specific
void recv_file_info(int rank, char *name_recv, int *nr_recv) {
  MPI_Status status;
  MPI_Recv(name_recv, MAX_FILENAME, MPI_CHAR, rank, INIT, MPI_COMM_WORLD,
           &status);
  MPI_Recv(nr_recv, 1, MPI_INT, rank, INIT, MPI_COMM_WORLD, &status);
}

// actualizeaza info despre un fisier existent pe baza datelor primite de la un
// client
void update_file(File *file, int rank, int nr_recv) {
  MPI_Status status;
  char hash_recv[HASH_SIZE];
  // seteaza clientul curent ca fiind parte din swarm-ul acestui fisier
  file->swarm[file->swarm_idx].client = rank;
  // primeste si modifica infor despre segmentele detinute
  for (int i = 0; i < nr_recv; i++) {
    MPI_Recv(hash_recv, HASH_SIZE, MPI_CHAR, rank, INIT, MPI_COMM_WORLD,
             &status);
    file->swarm[file->swarm_idx].seg_id[i] = 1;
  }
  // incrementeaza indexul swarm-ului pentru acest fisier
  file->swarm_idx++;
}

// adauga un nou fisier in sistem si initializeaza structurile
void add_new_file(File *file, int rank, int nr_recv, int numtasks) {
  file->seg = (Segment *)calloc(nr_recv, sizeof(Segment));
  file->total_seg = file->owned_seg = nr_recv;  // nr de segmente

  file->swarm = (Swarm *)calloc(numtasks, sizeof(Swarm));
  file->swarm[0].client = rank;  // primul client care detine fisierul
  file->swarm_idx = 0;

  for (int i = 0; i < numtasks; i++) {
    file->swarm[i].seg_id = (int *)calloc(nr_recv, sizeof(int));
  }

  MPI_Status status;
  // primeste si pastreaza hash urile segmentelor de la client
  for (int i = 0; i < nr_recv; i++) {
    MPI_Recv(file->seg[i].hash, HASH_SIZE, MPI_CHAR, rank, INIT, MPI_COMM_WORLD,
             &status);
    file->seg[i].owned = true;     // marcheaza segmentele ca detinute
    file->swarm[0].seg_id[i] = 1;  // marcheaza segmentul in swarm
  }
  file->swarm_idx = 1;  // seteaza indexul swarm
}

// initializeaza tracker ul pt a gestiona info despre fisierele detinute de
// clienti
void init_tracker(int numtasks, File *files, int *total_files) {
  MPI_Status status;
  char name_recv[MAX_FILENAME];  // buffer pt a primi numele fisierelor
  int nr_recv;                   // nr segmente primite
  int nr_client;                 // nr fisiere de la un client

  // itereaza prin fiecare client incepe cu 1 ca sa excluda tracker ul
  for (int i = 1; i < numtasks; i++) {
    // primeste nr de fisiere de la fiecare client
    MPI_Recv(&nr_client, 1, MPI_INT, i, INIT, MPI_COMM_WORLD, &status);
    // proceseaza fiecare fisier primit
    while (nr_client > 0) {
      bool new_file = true;
      // primeste info despre fisier
      recv_file_info(i, name_recv, &nr_recv);
      // verifica daca fisierul este deja cunoscut de tracker
      bool ok = true;
      for (int j = 0; j < *total_files && ok; j++) {
        if (strcmp(name_recv, files[j].name) == 0) {
          new_file = false;
          update_file(&files[j], i, nr_recv);
          ok = false;
        }
      }
      // daca fisierul este nou il adauga in lista de fisiere
      if (new_file) {
        strcpy(files[*total_files].name, name_recv);
        add_new_file(&files[*total_files], i, nr_recv, numtasks);
        (*total_files)++;
      }

      nr_client--;
    }
  }

  // trimite un mesaj catre toate procesele ca initiazilarea s a incheiat
  for (int i = 1; i < numtasks; i++) {
    MPI_Send("OK", 2, MPI_CHAR, i, 0, MPI_COMM_WORLD);
  }
}

void update_seg(Swarm *swarm, int nr_recv, MPI_Status *status, int rank) {
  char hash_recv[HASH_SIZE];
  for (int i = 0; i < nr_recv; i++) {
    MPI_Recv(hash_recv, HASH_SIZE, MPI_CHAR, rank, UPDATE, MPI_COMM_WORLD,
             status);
    swarm->seg_id[i] = 1;
  }
}

// actualizeaza info despre un fisier pe baza datelor primite de la un client
void update_file(File *file, int rank, MPI_Status *status) {
  int nr_recv;  // nr de segmente actualizate
  MPI_Recv(&nr_recv, 1, MPI_INT, rank, UPDATE, MPI_COMM_WORLD, status);

  // gaseste sau initializeaza un swarm pt clientul dat
  int swarm_idx = file->swarm_idx;
  bool ok = true;
  for (int i = 0; i < file->swarm_idx && ok; i++) {
    if (file->swarm[i].client == rank) {
      swarm_idx = i;
      ok = false;
    }
  }

  // actualizeaza sau adauga info despre segmentele detinute
  file->swarm[swarm_idx].client = rank;
  update_seg(&file->swarm[swarm_idx], nr_recv, status, rank);

  // daca este un swarm nou incrementeaz indexul
  if (swarm_idx == file->swarm_idx) {
    file->swarm_idx++;
  }
}

// gestionarea actualizarilor de la clienti
void tag_update(int rank, File *files, int total_files, MPI_Status *status) {
  int nr_client;  // nr de actualizari de procesat
  MPI_Recv(&nr_client, 1, MPI_INT, rank, UPDATE, MPI_COMM_WORLD, status);

  // proceseaza fiecare actualizare primita
  while (nr_client) {
    char name_recv[MAX_FILENAME];
    MPI_Recv(name_recv, MAX_FILENAME, MPI_CHAR, rank, UPDATE, MPI_COMM_WORLD,
             status);
    // cauta fisierul respectiv si aplica actualizarile
    bool ok = true;
    for (int i = 0; i < total_files && ok; i++) {
      if (strcmp(name_recv, files[i].name) == 0) {
        update_file(&files[i], rank, status);
        ok = false;
      }
    }
    nr_client--;
  }

  // trimite confirmare ca actualizarea a fost primita si procesata
  MPI_Send("OK", 2, MPI_CHAR, rank, UPDATE, MPI_COMM_WORLD);
}

// gestionarea proceselor cu clientii pe durata sesiunii de download
void download_tracker(int numtasks, File *files, int total_files) {
  int nr = numtasks - 1;
  int recv_tag, rank;

  MPI_Status status;
  char data_recv[MAX_FILENAME];

  // Bucla pana cand toate procesele au raportat completarea
  while (nr) {
    MPI_Recv(data_recv, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG,
             MPI_COMM_WORLD, &status);

    recv_tag = status.MPI_TAG;
    rank = status.MPI_SOURCE;
    // gestioneaza actualizarile sau cererile in functie de tag-ul mesajului
    if (recv_tag == UPDATE) {
      tag_update(rank, files, total_files, &status);
    } else if (recv_tag == SWARM_REQ) {
      send_swarm(rank, files, total_files, data_recv);
    } else if (recv_tag == FINISHED) {
      nr--;
    }
  }

  // trimite un semnal de stop tuturor proceselor dupa ce toate au finalizat
  for (int i = 1; i < numtasks; i++) {
    send_msg("STOP", i, SEG_REQ);
  }
}

// functia principala a tracker ului
void tracker(int numtasks, int rank) {
  File files[MAX_FILES];
  int total_files = 0;
  // initializarea tracker ului
  init_tracker(numtasks, files, &total_files);
  // gestionarea descarcarilor si aa actualizarilor
  download_tracker(numtasks, files, total_files);
}

// functia principala a clientului
void peer(int numtasks, int rank) {
  pthread_t download_thread;
  pthread_t upload_thread;
  void *status;
  int r;
  // citeste datele clientului
  read(rank);

  r = pthread_create(&download_thread, NULL, download_thread_func,
                     (void *)&rank);
  if (r) {
    printf("Eroare la crearea thread-ului de download\n");
    exit(-1);
  }

  r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *)&rank);
  if (r) {
    printf("Eroare la crearea thread-ului de upload\n");
    exit(-1);
  }

  r = pthread_join(download_thread, &status);
  if (r) {
    printf("Eroare la asteptarea thread-ului de download\n");
    exit(-1);
  }

  r = pthread_join(upload_thread, &status);
  if (r) {
    printf("Eroare la asteptarea thread-ului de upload\n");
    exit(-1);
  }
  // elibereaza resursele alocate
  free_data(files, nr_owned);
}

int main(int argc, char *argv[]) {
  int numtasks, rank;

  int provided;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
  if (provided < MPI_THREAD_MULTIPLE) {
    fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
    exit(-1);
  }
  MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (rank == TRACKER_RANK) {
    tracker(numtasks, rank);
  } else {
    peer(numtasks, rank);
  }

  MPI_Finalize();
}
