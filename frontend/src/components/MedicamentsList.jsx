export default function MedicamentsList({ medicaments, onDelete, onConfirmIntake, onShareReminder, loading }) {
  return (
    <section className="card list-card">
      <div className="section-heading">
        <p className="eyebrow">Suivi</p>
        <h2>Médicaments enregistrés</h2>
      </div>

      {loading ? (
        <p className="muted">Chargement des médicaments...</p>
      ) : medicaments.length === 0 ? (
        <p className="muted">Aucun médicament n’a encore été ajouté.</p>
      ) : (
        <ul className="med-list">
          {medicaments.map((item) => (
            <li key={item.id} className="med-item">
              <div className="med-item-main">
                {item.photo_url ? <img src={item.photo_url} alt={item.nom} className="med-photo" /> : null}
                <strong>{item.nom}</strong>
                <span>{item.dose}</span>
                <span>
                  Stock: {item.quantite_restante ?? 0} (seuil: {item.seuil_alerte ?? 0})
                </span>
                {Number(item.quantite_restante ?? 0) <= Number(item.seuil_alerte ?? 0) ? (
                  <span className="stock-alert">Stock faible: pense a racheter une boite.</span>
                ) : null}
              </div>
              <div className="med-item-aside">
                <span className="time-pill">{item.heure}</span>
                <button className="ghost-button" type="button" onClick={() => onConfirmIntake(item)}>
                  Confirmer la prise
                </button>
                <button className="ghost-button" type="button" onClick={() => onShareReminder(item)}>
                  Partager rappel
                </button>
                <button className="ghost-button" type="button" onClick={() => onDelete(item.id)}>
                  Supprimer
                </button>
              </div>
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
