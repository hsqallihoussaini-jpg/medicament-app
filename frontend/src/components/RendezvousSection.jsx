import { useMemo, useState } from 'react';

const initialForm = {
  titre: '',
  medecin: '',
  date_heure: '',
  lieu: '',
  notes: '',
  rappel_minutes: '60',
};

function clampPercent(value) {
  if (Number.isNaN(value) || !Number.isFinite(value)) {
    return 0;
  }
  return Math.max(0, Math.min(100, value));
}

export default function RendezvousSection({ rendezvous, loading, onCreate, onDelete }) {
  const [form, setForm] = useState(initialForm);
  const [error, setError] = useState('');

  const sorted = useMemo(() => {
    return [...rendezvous].sort((a, b) => new Date(a.date_heure).getTime() - new Date(b.date_heure).getTime());
  }, [rendezvous]);

  const now = Date.now();
  const next = sorted.find((item) => new Date(item.date_heure).getTime() > now);
  const previous = [...sorted].reverse().find((item) => new Date(item.date_heure).getTime() <= now);

  let progressPercent = 0;
  if (next && previous) {
    const previousTs = new Date(previous.date_heure).getTime();
    const nextTs = new Date(next.date_heure).getTime();
    if (nextTs > previousTs) {
      progressPercent = clampPercent(((now - previousTs) / (nextTs - previousTs)) * 100);
    }
  }

  async function handleSubmit(event) {
    event.preventDefault();
    setError('');

    if (!form.titre.trim() || !form.medecin.trim() || !form.date_heure.trim()) {
      setError('Titre, médecin et date/heure sont obligatoires.');
      return;
    }

    try {
      await onCreate({
        titre: form.titre.trim(),
        medecin: form.medecin.trim(),
        date_heure: new Date(form.date_heure).toISOString(),
        lieu: form.lieu.trim(),
        notes: form.notes.trim(),
        rappel_minutes: Number.parseInt(form.rappel_minutes, 10) || 60,
      });
      setForm(initialForm);
    } catch (submitError) {
      setError(submitError.message || 'Impossible d’ajouter le rendez-vous.');
    }
  }

  function updateField(event) {
    const { name, value } = event.target;
    setForm((current) => ({ ...current, [name]: value }));
  }

  return (
    <section className="card rendezvous-card">
      <div className="section-heading">
        <p className="eyebrow">Suivi médical</p>
        <h2>Rendez-vous médicaux</h2>
      </div>

      <form className="med-form" onSubmit={handleSubmit}>
        <label>
          <span>Titre</span>
          <input name="titre" value={form.titre} onChange={updateField} placeholder="Contrôle cardiologie" />
        </label>
        <label>
          <span>Médecin</span>
          <input name="medecin" value={form.medecin} onChange={updateField} placeholder="Dr. Dupont" />
        </label>
        <label>
          <span>Date et heure</span>
          <input name="date_heure" type="datetime-local" value={form.date_heure} onChange={updateField} />
        </label>
        <label>
          <span>Lieu</span>
          <input name="lieu" value={form.lieu} onChange={updateField} placeholder="Clinique Saint-Paul" />
        </label>
        <label>
          <span>Notes</span>
          <textarea name="notes" value={form.notes} onChange={updateField} rows={3} placeholder="Examens à apporter..." />
        </label>
        <label>
          <span>Rappel (minutes avant)</span>
          <input
            name="rappel_minutes"
            type="number"
            min="1"
            value={form.rappel_minutes}
            onChange={updateField}
          />
        </label>

        {error ? <p className="inline-error">{error}</p> : null}
        <button className="primary-button" type="submit" disabled={loading}>
          Ajouter le rendez-vous
        </button>
      </form>

      <div className="rv-progress">
        <h3>Progression entre rendez-vous</h3>
        {next ? (
          <>
            <p>
              Prochain rendez-vous: <strong>{next.titre}</strong> avec {next.medecin} le{' '}
              {new Date(next.date_heure).toLocaleString()}
            </p>
            <div className="adherence-bar">
              <span style={{ width: `${progressPercent}%` }} />
            </div>
            <p className="muted">
              {previous ? `Progression: ${Math.round(progressPercent)}% depuis le rendez-vous précédent.` : 'Aucun rendez-vous précédent pour calculer la progression.'}
            </p>
          </>
        ) : (
          <p className="muted">Aucun rendez-vous à venir.</p>
        )}
      </div>

      <div className="rv-list-wrap">
        <h3>Liste des rendez-vous</h3>
        {loading ? (
          <p className="muted">Chargement des rendez-vous...</p>
        ) : sorted.length === 0 ? (
          <p className="muted">Aucun rendez-vous planifié.</p>
        ) : (
          <ul className="history-list">
            {sorted.map((item) => (
              <li className="history-item" key={item.id}>
                <strong>{item.titre}</strong>
                <span>{item.medecin}</span>
                <span>{new Date(item.date_heure).toLocaleString()}</span>
                {item.lieu ? <span>Lieu: {item.lieu}</span> : null}
                <span>Rappel: {item.rappel_minutes} min avant</span>
                {item.notes ? <span>{item.notes}</span> : null}
                <button className="ghost-button" type="button" onClick={() => onDelete(item.id)}>
                  Supprimer
                </button>
              </li>
            ))}
          </ul>
        )}
      </div>
    </section>
  );
}
