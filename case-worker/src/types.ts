export type Region = "eu" | "na" | "jp";

export interface Selectors {
  loggedInMarker: string;
  loginEmail: string;
  threadMessage: string;
  threadMessageAuthor: string;
  threadMessageBody: string;
  threadMessageTime: string;
  replyButton: string;    // opens the reply editor
  replyTextarea: string;  // the reply text box
  attachInput: string;    // <input type=file> for attachments
  sendButton: string;     // submits the reply
  caseSubject: string;
  caseStatus: string;
}

export interface RegionConfig {
  /** Seller Central domain for this region, e.g. sellercentral.amazon.co.uk / .com / .co.jp */
  domain: string;
  /** Persistent Chromium profile dir — one login session per region. */
  userDataDir: string;
  locale: string;
  /**
   * Marketplace/country to pick on Amazon's account-switcher interstitial
   * (the "Select an account" page), e.g. "United Kingdom". If unset, the
   * currently-checked account is used as-is.
   */
  accountCountry?: string;
}

export interface Config {
  regions: Record<Region, RegionConfig>;
  defaultRegion: Region;
  headless: boolean;
  slowMoMs: number;
  screenshotDir: string;
  serverPort: number;
  caseUrlTemplate: string;
  caseListUrl: string;
  selectors: Selectors;
}

export interface ThreadMessage {
  from: "amazon" | "seller" | "unknown";
  author: string;
  text: string;
  ts: string;
}

export interface CaseThread {
  caseId: string;
  region: Region;
  subject: string;
  status: string;
  messages: ThreadMessage[];
  /** Stable hash of the message contents; lets the app detect new Amazon replies. */
  threadHash: string;
}

/** JSON-lines request over TCP: one JSON object per line. */
export interface RpcRequest {
  id: string;
  method: "ping" | "getThread" | "fillReply" | "send" | "openCase";
  params?: {
    /** Which Seller Central region to drive. Defaults to config.defaultRegion. */
    region?: Region;
    caseId?: string;
    text?: string;
  };
}

export interface RpcResponse {
  id: string;
  ok: boolean;
  result?: unknown;
  error?: string;
}
