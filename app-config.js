// One-time shared configuration for all devices.
// Fill these values from Supabase Project Settings > API.
window.APP_CONFIG = {
  // Example: https://YOUR_PROJECT_REF.supabase.co
  apiBase: 'https://nwvwqmcezaymypkictil.supabase.co/rest/v1/',

  // Gate mode: token (Wi-Fi claim only)
  gateMode: 'token',

  // ESP32 gate endpoint on classroom Wi-Fi (STA + DHCP).
  espGateUrl: 'http://192.168.1.65',

  // Example: https://YOUR_SERVER_HOST (must host /api/gate/validate)
  gateApiBase: 'http://192.168.1.51:8000',

  // When true, check-in is blocked unless a valid gate token is present.
  gateRequired: true,

  // Example: eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
  supabaseAnonKey: 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Im53dndxbWNlemF5bXlwa2ljdGlsIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODI1NzI5MjAsImV4cCI6MjA5ODE0ODkyMH0.sv03G8WixXmy6cx4P5PZvBdbJK6yQAPLZd60C__KeEI'
};
