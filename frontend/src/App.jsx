import { useEffect, useState } from 'react';

import {
  askQuestion,
  createMedicament,
  createPrise,
  createRendezvous,
  deleteMedicament,
  deleteRendezvous,
  fetchMedicaments,
  fetchPrises,
  fetchRendezvous,
  fetchStats,
  shareRappel,
  updateMedicamentStock,
} from './api/client';
import { supabase } from './api/supabaseClient';
import AdherenceStats from './components/AdherenceStats';
import ChatBox from './components/ChatBox';
import MedicamentsForm from './components/MedicamentsForm';
import MedicamentsList from './components/MedicamentsList';
import PriseHistory from './components/PriseHistory';
import RendezvousSection from './components/RendezvousSection';

export default function App({ session }) {
  const [activeSpace, setActiveSpace] = useState('rappels');
  const [medicaments, setMedicaments] = useState([]);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [prises, setPrises] = useState([]);
  const [rendezvous, setRendezvous] = useState([]);
  const [stats, setStats] = useState(null);
  const [error, setError] = useState('');

  async function refreshPrisesAndStats() {
    const [prisesData, statsData] = await Promise.all([fetchPrises(), fetchStats()]);
    setPrises(Array.isArray(prisesData) ? prisesData : []);
    setStats(statsData || null);
  }

  useEffect(() => {
    async function loadMedicaments() {
      try {
        const [data, rendezvousData] = await Promise.all([fetchMedicaments(), fetchRendezvous()]);
        setMedicaments(Array.isArray(data) ? data : []);
        setRendezvous(Array.isArray(rendezvousData) ? rendezvousData : []);
        await refreshPrisesAndStats();
      } catch (fetchError) {
        setError(fetchError.message || 'Impossible de charger les médicaments.');
      } finally {
        setLoading(false);
      }
    }

    loadMedicaments();
  }, []);

  useEffect(() => {
    if (!('Notification' in window)) {
      return;
    }

    if (Notification.permission === 'default') {
      Notification.requestPermission();
    }

    const interval = setInterval(() => {
      if (Notification.permission !== 'granted') {
        return;
      }

      const now = new Date();
      const currentMinutes = now.getHours() * 60 + now.getMinutes();
      const today = now.toISOString().slice(0, 10);

      medicaments.forEach((medicament) => {
        if (!medicament.heure) {
          return;
        }

        const [hours, minutes] = medicament.heure.split(':').map((value) => Number.parseInt(value, 10) || 0);
        const medMinutes = hours * 60 + minutes;
        const alreadyTaken = prises.some(
          (prise) =>
            Number(prise.medicament_id) === Number(medicament.id) &&
            String(prise.taken_at || '').startsWith(today),
        );

        if (currentMinutes >= medMinutes && !alreadyTaken) {
          new Notification(`Rappel: ${medicament.nom}`, {
            body: `Il est temps de prendre ${medicament.dose}. Confirmation requise.`,
          });
        }
      });
    }, 60000);

    return () => clearInterval(interval);
  }, [medicaments, prises]);

  useEffect(() => {
    if (!('Notification' in window) || Notification.permission !== 'granted') {
      return;
    }

    const interval = setInterval(() => {
      const now = Date.now();

      rendezvous.forEach((rv) => {
        const rendezvousTs = new Date(rv.date_heure).getTime();
        const diffMinutes = Math.floor((rendezvousTs - now) / 60000);
        const reminderMinutes = Number(rv.rappel_minutes ?? 60);

        if (diffMinutes <= reminderMinutes && diffMinutes >= 0) {
          new Notification(`Rappel rendez-vous: ${rv.titre}`, {
            body: `Avec ${rv.medecin} dans ${diffMinutes} minute(s).`,
          });
        }
      });
    }, 60000);

    return () => clearInterval(interval);
  }, [rendezvous]);

  async function handleCreateMedicament(payload) {
    setSaving(true);
    setError('');
    try {
      const created = await createMedicament(payload);
      setMedicaments((current) => [created, ...current]);
    } catch (createError) {
      setError(createError.message || 'Impossible d’ajouter le médicament.');
      throw createError;
    } finally {
      setSaving(false);
    }
  }

  async function handleDeleteMedicament(id) {
    setError('');
    try {
      await deleteMedicament(id);
      setMedicaments((current) => current.filter((item) => item.id !== id));
    } catch (deleteError) {
      setError(deleteError.message || 'Impossible de supprimer le médicament.');
    }
  }

  async function handleConfirmIntake(medicament) {
    setError('');
    try {
      await createPrise({
        medicament_id: medicament.id,
        statut: 'prise_confirmee',
        commentaire: 'Confirmation depuis l’application',
      });

      const currentStock = Number(medicament.quantite_restante ?? 0);
      const nextStock = currentStock > 0 ? currentStock - 1 : 0;
      const updated = await updateMedicamentStock(medicament.id, nextStock);

      setMedicaments((current) =>
        current.map((item) => (item.id === medicament.id ? { ...item, ...updated } : item)),
      );
      await refreshPrisesAndStats();
    } catch (confirmError) {
      setError(confirmError.message || 'Impossible de confirmer la prise.');
    }
  }

  async function handleShareReminder(medicament) {
    setError('');
    if (!medicament.proche_contact) {
      setError('Aucun contact proche enregistré pour ce médicament.');
      return;
    }

    try {
      await shareRappel({
        medicament_id: medicament.id,
        destinataire: medicament.proche_contact,
        message: `Rappel partage: ${medicament.nom} (${medicament.dose}) a ${medicament.heure}.`,
      });
      await refreshPrisesAndStats();
    } catch (shareError) {
      setError(shareError.message || 'Impossible de partager le rappel.');
    }
  }

  async function handleLogout() {
    await supabase.auth.signOut();
  }

  async function handleQuestion(question) {
    const data = await askQuestion(question);
    return data?.response || data?.message || 'Réponse vide.';
  }

  async function handleCreateRendezvous(payload) {
    setError('');
    const created = await createRendezvous(payload);
    setRendezvous((current) => [...current, created].sort((a, b) => new Date(a.date_heure) - new Date(b.date_heure)));
  }

  async function handleDeleteRendezvous(id) {
    setError('');
    try {
      await deleteRendezvous(id);
      setRendezvous((current) => current.filter((item) => item.id !== id));
    } catch (deleteError) {
      setError(deleteError.message || 'Impossible de supprimer le rendez-vous.');
    }
  }

  return (
    <div className="app-shell">
      <div className="ambient ambient-one" />
      <div className="ambient ambient-two" />

      <main className="app-container">
        <header className="hero card">
          <div className="hero-copy">
            <p className="eyebrow">Gestion des traitements</p>
            <h1>Medicament App</h1>
            <p className="hero-text">
              Planifie les prises, visualise ta liste de médicaments et interroge Gemini depuis une seule interface.
            </p>
            {session?.user?.email ? (
              <div className="hero-account">
                <span className="muted">Connecté en tant que {session.user.email}</span>
                <button type="button" className="ghost-button" onClick={handleLogout}>
                  Se déconnecter
                </button>
              </div>
            ) : null}
          </div>

          <div className="stats-grid">
            <article>
              <span className="stat-value">{medicaments.length}</span>
              <span className="stat-label">Médicaments suivis</span>
            </article>
            <article>
              <span className="stat-value">{rendezvous.length}</span>
              <span className="stat-label">Rendez-vous planifiés</span>
            </article>
          </div>
        </header>

        {error ? <p className="page-error">{error}</p> : null}

        <section className="space-switch card">
          <button
            type="button"
            className={`space-button ${activeSpace === 'rappels' ? 'active' : ''}`}
            onClick={() => setActiveSpace('rappels')}
          >
            Rappels médicaments
          </button>
          <button
            type="button"
            className={`space-button ${activeSpace === 'rendezvous' ? 'active' : ''}`}
            onClick={() => setActiveSpace('rendezvous')}
          >
            Rendez-vous médicaux
          </button>
        </section>

        {activeSpace === 'rappels' ? (
          <>
            <section className="content-grid">
              <MedicamentsForm onSubmit={handleCreateMedicament} loading={saving} />
              <MedicamentsList
                medicaments={medicaments}
                onDelete={handleDeleteMedicament}
                onConfirmIntake={handleConfirmIntake}
                onShareReminder={handleShareReminder}
                loading={loading}
              />
            </section>

            <section className="content-grid">
              <AdherenceStats stats={stats} />
              <PriseHistory prises={prises} loading={loading} />
            </section>

            <section className="content-grid">
              <ChatBox onAsk={handleQuestion} />
            </section>
          </>
        ) : null}

        {activeSpace === 'rendezvous' ? (
          <section className="content-grid">
            <RendezvousSection
              rendezvous={rendezvous}
              loading={loading}
              onCreate={handleCreateRendezvous}
              onDelete={handleDeleteRendezvous}
            />
          </section>
        ) : null}
      </main>
    </div>
  );
}
