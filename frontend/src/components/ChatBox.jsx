import { useState } from 'react';

export default function ChatBox({ onAsk }) {
  const [question, setQuestion] = useState('');
  const [messages, setMessages] = useState([
    {
      role: 'assistant',
      content: 'Pose une question sur tes médicaments, les horaires ou les bonnes pratiques.',
    },
  ]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  async function handleSubmit(event) {
    event.preventDefault();
    const trimmed = question.trim();
    if (!trimmed || loading) {
      return;
    }

    setError('');
    setMessages((current) => [...current, { role: 'user', content: trimmed }]);
    setQuestion('');
    setLoading(true);

    try {
      const answer = await onAsk(trimmed);
      setMessages((current) => [...current, { role: 'assistant', content: answer }]);
    } catch (submitError) {
      setError(submitError.message || 'Impossible de joindre Gemini.');
    } finally {
      setLoading(false);
    }
  }

  return (
    <section className="card chat-card">
      <div className="section-heading">
        <p className="eyebrow">Assistant IA</p>
        <h2>Chatbox Gemini</h2>
      </div>

      <div className="chat-stream" aria-live="polite">
        {messages.map((message, index) => (
          <article key={`${message.role}-${index}`} className={`bubble ${message.role}`}>
            {message.content}
          </article>
        ))}
      </div>

      {error ? <p className="inline-error">{error}</p> : null}

      <form className="chat-form" onSubmit={handleSubmit}>
        <textarea
          value={question}
          onChange={(event) => setQuestion(event.target.value)}
          placeholder="Ex. Quel est le meilleur moment pour prendre le traitement ?"
          rows={3}
        />
        <button className="primary-button" type="submit" disabled={loading}>
          {loading ? 'Réponse en cours...' : 'Envoyer'}
        </button>
      </form>
    </section>
  );
}
