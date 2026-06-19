import { useState } from 'react';

import { supabase } from '../api/supabaseClient';

export default function Auth() {
  const [mode, setMode] = useState('signin');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const [info, setInfo] = useState('');

  async function handleSubmit(event) {
    event.preventDefault();
    setError('');
    setInfo('');

    const trimmedEmail = email.trim();
    if (!trimmedEmail || !password) {
      setError('Renseigne ton email et ton mot de passe.');
      return;
    }

    setLoading(true);
    try {
      if (mode === 'signin') {
        const { error: signInError } = await supabase.auth.signInWithPassword({
          email: trimmedEmail,
          password,
        });
        if (signInError) {
          throw signInError;
        }
      } else {
        const { data, error: signUpError } = await supabase.auth.signUp({
          email: trimmedEmail,
          password,
        });
        if (signUpError) {
          throw signUpError;
        }
        if (!data.session) {
          setInfo('Compte créé. Vérifie ta boîte mail pour confirmer ton adresse.');
        }
      }
    } catch (authError) {
      setError(authError.message || 'Authentification impossible.');
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="auth-shell">
      <div className="ambient ambient-one" />
      <div className="ambient ambient-two" />

      <section className="card auth-card">
        <div className="auth-brand">
          <p className="eyebrow">Espace sécurisé</p>
          <h1>Medicament App</h1>
          <p className="muted">
            {mode === 'signin'
              ? 'Connecte-toi pour accéder à ton suivi de traitement.'
              : 'Crée ton compte pour suivre tes traitements en toute sécurité.'}
          </p>
        </div>

        <form className="med-form" onSubmit={handleSubmit}>
          <label>
            <span>Email</span>
            <input
              type="email"
              autoComplete="email"
              value={email}
              onChange={(event) => setEmail(event.target.value)}
              placeholder="prenom@exemple.com"
            />
          </label>

          <label>
            <span>Mot de passe</span>
            <input
              type="password"
              autoComplete={mode === 'signin' ? 'current-password' : 'new-password'}
              value={password}
              onChange={(event) => setPassword(event.target.value)}
              placeholder="••••••••"
            />
          </label>

          {error ? <p className="inline-error">{error}</p> : null}
          {info ? <p className="auth-info">{info}</p> : null}

          <button className="primary-button" type="submit" disabled={loading}>
            {loading
              ? 'Patiente...'
              : mode === 'signin'
                ? 'Se connecter'
                : 'Créer un compte'}
          </button>
        </form>

        <p className="auth-switch">
          {mode === 'signin' ? 'Pas encore de compte ?' : 'Déjà inscrit ?'}{' '}
          <button
            type="button"
            className="auth-link"
            onClick={() => {
              setMode(mode === 'signin' ? 'signup' : 'signin');
              setError('');
              setInfo('');
            }}
          >
            {mode === 'signin' ? 'Créer un compte' : 'Se connecter'}
          </button>
        </p>
      </section>
    </div>
  );
}
