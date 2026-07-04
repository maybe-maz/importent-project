// One-time shared configuration for all devices.
// Fill these values from Supabase Project Settings > API.
window.APP_CONFIG = {
  // Example: https://YOUR_PROJECT_REF.supabase.co
  apiBase: 'https://nwvwqmcezaymypkictil.supabase.co/rest/v1/',

  // Gate mode: hybrid | bluetooth | token
  gateMode: 'hybrid',

  // iOS fallback gate endpoint (ESP32 AP HTTP claim).
  espGateUrl: 'http://192.168.4.1',

  // BLE GATT service and characteristics exposed by ESP32 beacon.
  bleServiceUuid: '4fafc201-1fb5-459e-8fcc-c5c9c331914b',
  bleLectureIdCharUuid: 'beb5483e-36e1-4688-b7f5-ea07361b26a8',
  bleTokenCharUuid: '0f3f2e60-8e92-4ea7-9f93-8b4b31c0d9aa',

  // Example: https://YOUR_SERVER_HOST (must host /api/gate/validate)
  gateApiBase: 'http://localhost:8000',

  // When true, check-in is blocked unless a valid gate token is present.
  gateRequired: true,

  // Example: eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
  supabaseAnonKey: 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Im53dndxbWNlemF5bXlwa2ljdGlsIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODI1NzI5MjAsImV4cCI6MjA5ODE0ODkyMH0.sv03G8WixXmy6cx4P5PZvBdbJK6yQAPLZd60C__KeEI'
};
