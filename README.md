# Medicament App

Application web de gestion de médicaments avec :

- un backend en C sur le port `8080` utilisant `libcurl` et `cJSON`
- un frontend React avec formulaire d'ajout, liste et chatbox IA
- une base Supabase pour stocker les médicaments
- une intégration Google Gemini pour les questions en langage naturel

## Structure

```text
medicament_app/
├─ backend/
│  ├─ CMakeLists.txt
│  ├─ run.ps1
│  ├─ .env.example
│  ├─ include/
│  └─ src/
├─ frontend/
│  ├─ package.json
│  ├─ vite.config.js
│  ├─ .env.example
│  └─ src/
├─ scripts/
│  └─ check-prereqs.ps1
├─ run-frontend.ps1
├─ supabase/
│  └─ schema.sql
└─ README.md
```

## Prérequis

- Node.js 18+
- Un compilateur C compatible C11
- CMake 3.20+
- `libcurl`
- `cJSON`
- Un projet Supabase avec table `medicaments`
- Une clé API Google Gemini

### Installation des dépendances C avec vcpkg sur Windows

```powershell
vcpkg install curl cjson
```

Puis configurer CMake avec le toolchain vcpkg.

### Alternative MSYS2 (sans CMake)

Si `cmake` n'est pas disponible, tu peux compiler le backend via `gcc` avec :

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-curl mingw-w64-x86_64-cjson
```

Le script `backend/run.ps1` tente d'abord CMake, puis un fallback GCC.

## Base Supabase

Exécute le script SQL dans [supabase/schema.sql](supabase/schema.sql) pour créer la table.

Champs attendus :

- `id` UUID ou bigint auto-généré
- `nom` texte
- `dose` texte
- `heure` texte
- `created_at` timestamp

## Configuration

### Backend

Créer [backend/.env.example](backend/.env.example) en `.env` et renseigner :

- `SUPABASE_URL`
- `SUPABASE_ANON_KEY`
- `GEMINI_API_KEY`
- `PORT` optionnel, par défaut `8080`

### Frontend

Créer [frontend/.env.example](frontend/.env.example) en `.env` et renseigner :

- `VITE_API_BASE_URL=http://localhost:8080`
- `VITE_SUPABASE_URL` : URL du projet Supabase (Project Settings → API)
- `VITE_SUPABASE_ANON_KEY` : clé `anon`/publique Supabase

L'accès au frontend est protégé par une authentification Supabase (email/mot de passe). Active l'authentification par email dans ton projet Supabase et, pour déployer sur Vercel, ajoute `VITE_SUPABASE_URL` et `VITE_SUPABASE_ANON_KEY` dans les variables d'environnement du projet.

## Lancement du backend

Option recommandée (script automatique):

```powershell
cd backend
.\run.ps1
```

Ce script :

- crée `.env` depuis `.env.example` si besoin
- utilise CMake si disponible
- sinon tente une compilation GCC avec `libcurl` et `cJSON`

Option manuelle CMake :

```powershell
cd backend
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
.
\build\Release\medicament_backend.exe
```

Si tu utilises un générateur Unix ou MinGW, adapte le chemin de l'exécutable généré.

## Lancement du frontend

Option rapide :

```powershell
.\run-frontend.ps1
```

Option manuelle :

```powershell
cd frontend
npm install
npm run dev
```

Le frontend parle au backend sur `http://localhost:8080`.

## API backend

- `GET /medicaments` : liste tous les médicaments
- `POST /medicaments` : ajoute un médicament `{ nom, dose, heure }`
- `PATCH /medicaments/:id/stock` : met à jour le stock `{ quantite_restante }`
- `DELETE /medicaments/:id` : supprime un médicament
- `GET /prises` : historique des prises (option `?medicament_id=`)
- `POST /prises` : confirme une prise `{ medicament_id, statut, commentaire }`
- `GET /stats` : statistiques de respect et stock faible
- `POST /partage-rappel` : partage un rappel `{ medicament_id, destinataire, message }`
- `GET /rendezvous` : liste des rendez-vous médicaux
- `POST /rendezvous` : ajoute un rendez-vous `{ titre, medecin, date_heure, lieu, notes, rappel_minutes }`
- `DELETE /rendezvous/:id` : supprime un rendez-vous
- `POST /chat` : envoie une question à Gemini `{ question }`

## Notes techniques

- Le backend sert une API JSON et ajoute les en-têtes CORS.
- Les données sont stockées dans Supabase via l'API REST `rest/v1`.
- Le chat utilise Gemini via `generateContent`.
- Le frontend déclenche des notifications répétées tant qu'une prise attendue n'est pas confirmée.
- Le stock faible est affiché dès que `quantite_restante <= seuil_alerte`.

## Démarrage rapide

1. Crée la table Supabase avec le script SQL.
2. Configure les variables d'environnement du backend et du frontend.
3. Vérifie les prérequis avec `powershell -File scripts/check-prereqs.ps1`.
4. Lance le backend sur le port `8080` via `backend/run.ps1`.
5. Lance le frontend React via `run-frontend.ps1`.
