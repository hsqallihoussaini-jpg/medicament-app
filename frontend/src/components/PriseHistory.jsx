export default function PriseHistory({ prises, loading }) {
  return (
    <section className="card history-card">
      <div className="section-heading">
        <p className="eyebrow">Historique</p>
        <h2>Historique des prises</h2>
      </div>

      {loading ? (
        <p className="muted">Chargement de l'historique...</p>
      ) : prises.length === 0 ? (
        <p className="muted">Aucune prise enregistree pour le moment.</p>
      ) : (
        <ul className="history-list">
          {prises.slice(0, 12).map((prise) => (
            <li key={prise.id} className="history-item">
              <strong>Medicament #{prise.medicament_id}</strong>
              <span>{prise.statut || 'prise_confirmee'}</span>
              <span>{new Date(prise.taken_at).toLocaleString()}</span>
              {prise.commentaire ? <span>{prise.commentaire}</span> : null}
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
