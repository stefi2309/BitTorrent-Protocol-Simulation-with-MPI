## IVANA STEFANIA 332CD

Protocolul este compus dintr-un nod principal numit tracker si mai multe noduri client care participa la partajarea si descarcarea fisierelor. Trackerul coordoneaza segmentele de fisier intre clienti, in timp ce clientii pot atat sa furnizeze segmente altor clienti, cat si sa descarce segmente de la acestia.

### Initializare:

La pornire, fiecare client citeste fisierele pe care le detine dintr-un fisier de intrare specificat si trimite aceste informatii trackerului.
Trackerul primeste informatiile de la toti clientii si construieste o structura interna care contine ce segmente din fiecare fisier sunt detinute de care client.

### Procesul de Download:

Fiecare client cere trackerului detalii despre segmentele necesare pt completarea fisierelor dorite.
Dupa primirea informatiilor, clientul incepe sa descarce segmentele lipsa de la alti clienti (seeds).

### Comunicarea:

Comunicarea intre tracker si clienti, si intre clienti, este realizata folosind mesaje MPI. Mesajele pot contine cereri pt segmente, raspunsuri cu segmente, sau actualizari de stare.

### Sincronizarea:

Sistemul foloseste mesaje pt a sincroniza starile intre tracker si clienti, asigurandu-se ca fiecare client are informatiile necesare pt a putea participa eficient la partajarea fisierelor.

### Structuri de Date Principale
- File: O structura care stocheaza informatii despre un fisier:  numele, segmentele, si informatii despre care clienti detin care segmente
- Segment: Reprezinta un semgent dintr-un fisier: hash-ul sau si starea daca este detinut sau nu
- Swarm: O structura asociata fiecarui fisier care tine evidenta clientilor ce detin segmente din acel fisier

### Functii Importante
- read() si read_segments(): Functii care citesc starea initiala a fisierelor detinute de un client
- req_swarm() si recv_swarm_info(): Cer si primesc informatii despre swarm
- send_seg_req() si recv_seg_info(): Trimite cereri pt segmente lipsa si primeste segmentele respective
- update_file() si tag_update(): Actualizeaza informatiile despre fisiere in tracker si gestioneaa actualizarile venite de la clieni
- download_thread_func() si upload_thread_func(): Functii executate in thread-uri separate pt gestionarea descarcarilor si respectiv incarcarilor de segmente.

### Concluzie
Tema foloseste o combinatie de MPI pt gestionarea comunicarilor in retea si Pthreads pt paralelizarea sarcinilor de upload si download in cadrul fiecarui client. 