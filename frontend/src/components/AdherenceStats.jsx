export default function AdherenceStats({ stats }) {
  const adherence = stats?.adherence_score ?? 0;

  return (
    <section className="card stats-card-extended">
      <div className="section-heading">
        <p className="eyebrow">Statistiques</p>
        <h2>Respect du traitement</h2>
      </div>

      <div className="adherence-grid">
        <article>
          <span className="stat-value">{stats?.total_medicaments ?? 0}</span>
          <span className="stat-label">Médicaments suivis</span>
        </article>
        <article>
          <span className="stat-value">{stats?.total_prises ?? 0}</span>
          <span className="stat-label">Prises confirmées</span>
        </article>
        <article>
          <span className="stat-value">{adherence}%</span>
          <span className="stat-label">Adhérence du jour</span>
        </article>
        <article>
          <span className="stat-value">{stats?.low_stock_count ?? 0}</span>
          <span className="stat-label">Alertes stock faible</span>
        </article>
      </div>

      <div className="adherence-bar">
        <span style={{ width: `${Math.max(0, Math.min(100, adherence))}%` }} />
      </div>
    </section>
  );
}
