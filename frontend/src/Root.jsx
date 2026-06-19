import { useEffect, useState } from 'react';

import { isSupabaseConfigured, supabase } from './api/supabaseClient';
import App from './App';
import Auth from './components/Auth';

export default function Root() {
  const [session, setSession] = useState(null);
  const [checking, setChecking] = useState(true);

  useEffect(() => {
    if (!isSupabaseConfigured) {
      setChecking(false);
      return undefined;
    }

    supabase.auth.getSession().then(({ data }) => {
      setSession(data.session);
      setChecking(false);
    });

    const {
      data: { subscription },
    } = supabase.auth.onAuthStateChange((_event, nextSession) => {
      setSession(nextSession);
    });

    return () => subscription.unsubscribe();
  }, []);

  if (!isSupabaseConfigured) {
    return (
      <div className="auth-shell">
        <section className="card auth-card">
          <div className="auth-brand">
            <p className="eyebrow">Configuration requise</p>
            <h1>Medicament App</h1>
          </div>
          <p className="inline-error">
            Authentification indisponible : renseigne <code>VITE_SUPABASE_URL</code> et{' '}
            <code>VITE_SUPABASE_ANON_KEY</code> dans le fichier <code>.env</code> du frontend.
          </p>
        </section>
      </div>
    );
  }

  if (checking) {
    return (
      <div className="auth-shell">
        <section className="card auth-card">
          <p className="muted">Chargement de la session...</p>
        </section>
      </div>
    );
  }

  if (!session) {
    return <Auth />;
  }

  return <App session={session} />;
}
