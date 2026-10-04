import { render } from 'preact';
import './styles.css';
import './ui/ui.css';
import { App } from './app';

const root = document.getElementById('root');
if (!root) throw new Error('[maho-web-ai] #root element not found');

render(<App />, root);
