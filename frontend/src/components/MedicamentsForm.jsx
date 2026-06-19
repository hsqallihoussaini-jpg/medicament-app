import { useState } from 'react';

const initialState = {
  nom: '',
  dose: '',
  heure: '',
  photo_url: '',
  quantite_restante: '0',
  seuil_alerte: '2',
  proche_nom: '',
  proche_contact: '',
};

export default function MedicamentsForm({ onSubmit, loading }) {
  const [form, setForm] = useState(initialState);
  const [error, setError] = useState('');

  function updateField(event) {
    const { name, value } = event.target;
    setForm((current) => ({ ...current, [name]: value }));
  }

  async function handleSubmit(event) {
    event.preventDefault();
    setError('');

    if (!form.nom.trim() || !form.dose.trim() || !form.heure.trim()) {
      setError('Remplis le nom, la dose et l’heure.');
      return;
    }

    try {
      await onSubmit({
        nom: form.nom.trim(),
        dose: form.dose.trim(),
        heure: form.heure.trim(),
        photo_url: form.photo_url.trim(),
        quantite_restante: Number.parseInt(form.quantite_restante, 10) || 0,
        seuil_alerte: Number.parseInt(form.seuil_alerte, 10) || 0,
        proche_nom: form.proche_nom.trim(),
        proche_contact: form.proche_contact.trim(),
      });
      setForm(initialState);
    } catch (submitError) {
      setError(submitError.message || 'Impossible d’ajouter le médicament.');
    }
  }

  return (
    <section className="card form-card">
      <div className="section-heading">
        <p className="eyebrow">Nouveau traitement</p>
        <h2>Ajouter un médicament</h2>
      </div>

      <form className="med-form" onSubmit={handleSubmit}>
        <label>
          <span>Nom</span>
          <input name="nom" value={form.nom} onChange={updateField} placeholder="Paracétamol" />
        </label>

        <label>
          <span>Dose</span>
          <input name="dose" value={form.dose} onChange={updateField} placeholder="500 mg" />
        </label>

        <label>
          <span>Heure</span>
          <input name="heure" type="time" value={form.heure} onChange={updateField} />
        </label>

        <label>
          <span>Photo (URL)</span>
          <input name="photo_url" value={form.photo_url} onChange={updateField} placeholder="https://..." />
        </label>

        <label>
          <span>Quantité restante</span>
          <input name="quantite_restante" type="number" min="0" value={form.quantite_restante} onChange={updateField} />
        </label>

        <label>
          <span>Seuil alerte stock faible</span>
          <input name="seuil_alerte" type="number" min="0" value={form.seuil_alerte} onChange={updateField} />
        </label>

        <label>
          <span>Nom du proche</span>
          <input name="proche_nom" value={form.proche_nom} onChange={updateField} placeholder="Nom du proche" />
        </label>

        <label>
          <span>Contact du proche</span>
          <input name="proche_contact" value={form.proche_contact} onChange={updateField} placeholder="Email ou téléphone" />
        </label>

        {error ? <p className="inline-error">{error}</p> : null}

        <button className="primary-button" type="submit" disabled={loading}>
          {loading ? 'Ajout en cours...' : 'Ajouter'}
        </button>
      </form>
    </section>
  );
}
