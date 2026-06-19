import { render } from 'preact';
import { App } from './app';
import './styles.css';

const root = document.getElementById('root');
if (!root) throw new Error('[maho-web-ai] #root element not found');

render(<App />, root);
